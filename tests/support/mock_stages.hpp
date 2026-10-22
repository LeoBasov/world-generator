#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/status.hpp"
#include "wg/pipeline/registry.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::test {

/// Behaviour of a MockStage and what it saw. Shared between the test and the stage instances
/// the pipeline owns, so the test can script a stage and inspect its calls afterwards.
struct MockScript {
  std::string id;
  pipeline::FieldSet inputs;
  pipeline::FieldSet outputs;

  /// Attempts on which run returns reject(check_id, measured, required).
  std::vector<std::uint32_t> reject_on;
  bool reject_always = false;
  std::string check_id = "MOCK_CHECK";
  double measured = 1.0;
  double required = 2.0;
  /// The repair hook succeeds (returns ok) instead of keeping the rejection.
  bool repairable = false;
  /// Requested by run after writing its outputs (a cancel arriving during the stage).
  core::CancelFlag* cancel_after_run = nullptr;
  /// Requested by the first chunk of the kernel that writes the outputs.
  core::CancelFlag* cancel_in_kernel = nullptr;
  /// Returned by run as an error unless ok.
  core::Status error;
  /// Leaves the declared output fields unwritten (a contract violation).
  bool skip_outputs = false;

  struct Call {
    std::uint32_t attempt = 0;
    core::RngKey key;
    const params::GenParams* params = nullptr;
    const params::DataTables* tables = nullptr;
    /// Some output field existed before the stage wrote it.
    bool outputs_present = false;
  };
  std::vector<Call> calls;
  int repairs = 0;
};

/// Stage driven by a MockScript. Writes every output field with values derived from its RNG
/// key through core::parallel_for (one chunk per row), adds a peak if it outputs peaks, and
/// reports the metric "cells".
class MockStage final : public pipeline::Stage {
 public:
  explicit MockStage(std::shared_ptr<MockScript> script) : s_(std::move(script)) {}

  [[nodiscard]] std::string_view id() const noexcept override { return s_->id; }
  [[nodiscard]] pipeline::FieldSet inputs() const noexcept override { return s_->inputs; }
  [[nodiscard]] pipeline::FieldSet outputs() const noexcept override { return s_->outputs; }

  pipeline::StageResult run(pipeline::StageContext& ctx) noexcept override {
    MockScript::Call call{.attempt = ctx.attempt,
                          .key = ctx.key,
                          .params = &ctx.params,
                          .tables = ctx.tables,
                          .outputs_present = false};
    s_->outputs.for_each([&](pipeline::FieldOrLayer item) {
      if (!item.is_layer && ctx.state.has(static_cast<core::FieldId>(item.index))) {
        call.outputs_present = true;
      }
    });
    s_->calls.push_back(call);
    if (!s_->error.ok()) {
      return pipeline::StageResult::error(s_->error);
    }
    if (!s_->skip_outputs) {
      core::Status status;
      s_->outputs.for_each([&](pipeline::FieldOrLayer item) {
        if (status.ok()) {
          status = write_output(ctx, item);
        }
      });
      if (!status.ok()) {
        return pipeline::StageResult::error(std::move(status));
      }
    }
    if (s_->cancel_after_run != nullptr) {
      s_->cancel_after_run->request();
    }
    const bool reject = s_->reject_always || std::find(s_->reject_on.begin(), s_->reject_on.end(),
                                                       ctx.attempt) != s_->reject_on.end();
    pipeline::StageResult result =
        reject ? pipeline::StageResult::reject(s_->check_id, s_->measured, s_->required)
               : pipeline::StageResult::ok();
    result.metrics.add("cells", static_cast<double>(ctx.state.grid().cells()));
    return result;
  }

  pipeline::StageResult repair(pipeline::StageContext& /*ctx*/,
                               const pipeline::StageResult& rejected) noexcept override {
    if (!s_->repairable) {
      return rejected;
    }
    ++s_->repairs;
    pipeline::StageResult result = pipeline::StageResult::ok();
    result.metrics.add("repaired_cells", 3.0);
    return result;
  }

 private:
  template <class T>
  core::Status fill(pipeline::StageContext& ctx, core::FieldId id) const noexcept {
    core::Expected<core::FieldView<T>> view = ctx.state.write<T>(id);
    if (!view) {
      return std::move(view.error());
    }
    const std::span<T> data = view->data;
    const std::int64_t n = ctx.state.grid().n;
    const core::RngKey key = ctx.key.derive(static_cast<std::uint64_t>(id));
    core::CancelFlag* cancel_in_kernel = s_->cancel_in_kernel;
    const auto body = [&](const core::ChunkRange& r) {
      if (cancel_in_kernel != nullptr && r.index == 0) {
        cancel_in_kernel->request();
      }
      for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
        data[static_cast<std::size_t>(i)] =
            static_cast<T>(core::bits64(key, static_cast<std::uint64_t>(i), 0) % 100U);
      }
    };
    return core::parallel_for(ctx.exec, 0, n, 1, body, &ctx.cancel);
  }

  core::Status write_output(pipeline::StageContext& ctx, pipeline::FieldOrLayer item) const {
    if (item.is_layer) {
      if (static_cast<pipeline::LayerId>(item.index) == pipeline::LayerId::peaks) {
        ctx.state.layers().peaks.push_back(core::Peak{.cell = 0, .height_eu = 1.0F});
      }
      return {};
    }
    const auto id = static_cast<core::FieldId>(item.index);
    switch (core::field_info(id).dtype) {
      case core::DType::u8:
        return fill<std::uint8_t>(ctx, id);
      case core::DType::u16:
        return fill<std::uint16_t>(ctx, id);
      case core::DType::i32:
        return fill<std::int32_t>(ctx, id);
      case core::DType::f32:
        return fill<float>(ctx, id);
    }
    return {};
  }

  std::shared_ptr<MockScript> s_;
};

/// Registers a MockStage factory for script->id.
inline core::Status add_mock(pipeline::StageRegistry& registry,
                             const std::shared_ptr<MockScript>& script) {
  return registry.add(script->id, [script] { return std::make_unique<MockStage>(script); });
}

/// Script with the given id, inputs and outputs.
inline std::shared_ptr<MockScript> mock(std::string id, pipeline::FieldSet inputs,
                                        pipeline::FieldSet outputs) {
  auto s = std::make_shared<MockScript>();
  s->id = std::move(id);
  s->inputs = inputs;
  s->outputs = outputs;
  return s;
}

}  // namespace wg::test
