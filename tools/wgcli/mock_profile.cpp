#include "mock_profile.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "wg/core/executor.hpp"
#include "wg/core/rng.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::cli {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

constexpr std::string_view kVerifyId = "m03_verify";

/// Writes every output field with values in [0, 100) from the stage key, one row per chunk.
class MockStage final : public pipeline::Stage {
 public:
  MockStage(std::string_view id, FieldSet inputs, FieldSet outputs) noexcept
      : id_(id), inputs_(inputs), outputs_(outputs) {}

  [[nodiscard]] std::string_view id() const noexcept override { return id_; }
  [[nodiscard]] FieldSet inputs() const noexcept override { return inputs_; }
  [[nodiscard]] FieldSet outputs() const noexcept override { return outputs_; }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "mock stage: out of memory"));
    }
  }

 private:
  StageResult run_checked(StageContext& ctx) {
    core::Status status;
    outputs_.for_each([&](pipeline::FieldOrLayer item) {
      if (status.ok()) {
        status = write_output(ctx, item);
      }
    });
    if (status.code == core::StatusCode::cancelled) {
      return StageResult::cancelled();
    }
    if (!status.ok()) {
      return StageResult::error(std::move(status));
    }
    StageResult result = StageResult::ok();
    if (id_ == kVerifyId) {
      const double u = core::uniform01(ctx.key, 0, 0);
      if (u < kMockRejectRate) {
        result = StageResult::reject("MOCK_VERIFY", u, kMockRejectRate);
      }
    }
    result.metrics.add("cells", static_cast<double>(ctx.state.grid().cells()));
    return result;
  }

  template <class T>
  static core::Status fill(StageContext& ctx, FieldId id) noexcept {
    core::Expected<core::FieldView<T>> view = ctx.state.write<T>(id);
    if (!view) {
      return std::move(view.error());
    }
    const std::span<T> data = view->data;
    const std::int64_t n = ctx.state.grid().n;
    const core::RngKey key = ctx.key.derive(static_cast<std::uint64_t>(id));
    const auto body = [&](const core::ChunkRange& r) {
      for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
        const std::uint64_t bits = core::bits64(key, static_cast<std::uint64_t>(i), 0);
        data[static_cast<std::size_t>(i)] = static_cast<T>(bits % 100U);
      }
    };
    return core::parallel_for(ctx.exec, 0, n, 1, body, &ctx.cancel);
  }

  static core::Status write_output(StageContext& ctx, pipeline::FieldOrLayer item) noexcept {
    if (item.is_layer) {
      if (static_cast<LayerId>(item.index) == LayerId::peaks) {
        const auto cells = static_cast<std::uint64_t>(ctx.state.grid().cells());
        const auto cell = static_cast<std::int64_t>(core::uniform_index(ctx.key, 1, 0, cells));
        try {
          ctx.state.layers().peaks.push_back(core::Peak{.cell = cell, .height_eu = 1.0F});
        } catch (...) {
          return core::make_status(core::StatusCode::out_of_memory, "mock stage: peaks");
        }
      }
      return {};
    }
    const auto id = static_cast<FieldId>(item.index);
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

  std::string_view id_;
  FieldSet inputs_;
  FieldSet outputs_;
};

core::Status add_stage(pipeline::StageRegistry& registry, std::string_view id, FieldSet inputs,
                       FieldSet outputs) noexcept {
  try {
    return registry.add(id, [id, inputs, outputs]() -> std::unique_ptr<pipeline::Stage> {
      return std::make_unique<MockStage>(id, inputs, outputs);
    });
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "cannot register mock stages");
  }
}

}  // namespace

core::Status add_mock_stages(pipeline::StageRegistry& registry) noexcept {
  if (core::Status s = add_stage(registry, "m01_seed", {}, {FieldId::latitude}); !s.ok()) {
    return s;
  }
  if (core::Status s = add_stage(registry, "m02_shape", {FieldId::latitude},
                                 {FieldId::elevation, LayerId::peaks});
      !s.ok()) {
    return s;
  }
  return add_stage(registry, kVerifyId, {FieldId::elevation, LayerId::peaks}, {FieldId::biome});
}

bool mock_verify_rejects(std::uint64_t seed, std::uint32_t attempt) noexcept {
  return core::uniform01(core::stage_key(seed, kVerifyId, attempt), 0, 0) < kMockRejectRate;
}

}  // namespace wg::cli
