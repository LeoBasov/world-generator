#include "wg/pipeline/pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "params/reader.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/version.hpp"
#include "wg/params/hash.hpp"

namespace wg::pipeline {

namespace {

using core::StatusCode;
using Kind = StageResult::Kind;

constexpr std::string_view kProgressScope = "pipeline";

std::string num(double value) { return params::detail::format_number(value); }

tl::unexpected<FailureReport> failure(core::Status status, RunReport& report) {
  return tl::unexpected<FailureReport>(FailureReport{std::move(status), std::move(report)});
}

tl::unexpected<FailureReport> failure(StatusCode code, std::string_view message,
                                      RunReport& report) {
  return failure(core::make_status(code, message), report);
}

void write_log(core::LogSink& sink, core::LogLevel level, const std::string& message) {
  if (sink.enabled(level)) {
    sink.write(level, message);
  }
}

template <core::FieldValue T>
core::Status copy_field(const core::WorldState& from, core::WorldState& to, core::FieldId id) {
  core::Expected<core::FieldView<const T>> src = from.read<T>(id);
  if (!src) {
    return std::move(src.error());
  }
  core::Expected<core::FieldView<T>> dst = to.write<T>(id);
  if (!dst) {
    return std::move(dst.error());
  }
  std::copy(src->data.begin(), src->data.end(), dst->data.begin());
  return {};
}

// Copies every field and the vector layers of the initial state into a fresh one.
core::Status copy_initial(const core::WorldState& from, core::WorldState& to) {
  for (const core::FieldInfo& f : core::kFields) {
    if (!from.has(f.id)) {
      continue;
    }
    core::Status s;
    switch (f.dtype) {
      case core::DType::u8:
        s = copy_field<std::uint8_t>(from, to, f.id);
        break;
      case core::DType::u16:
        s = copy_field<std::uint16_t>(from, to, f.id);
        break;
      case core::DType::i32:
        s = copy_field<std::int32_t>(from, to, f.id);
        break;
      case core::DType::f32:
        s = copy_field<float>(from, to, f.id);
        break;
    }
    if (!s.ok()) {
      return s;
    }
  }
  to.layers() = from.layers();
  return {};
}

// ok, or invalid_argument if the initial state does not fit the grid and the provided fields.
core::Status check_inputs(const FieldSet& provided, const core::Grid& grid,
                          const RunInputs& inputs) {
  if (inputs.initial == nullptr) {
    if (!provided.empty()) {
      return core::make_status(StatusCode::invalid_argument,
                               "the pipeline expects provided inputs (" + to_string(provided) +
                                   ") but the run has no initial state");
    }
    return {};
  }
  if (inputs.initial->grid() != grid) {
    return core::make_status(StatusCode::invalid_argument,
                             "the initial state's grid differs from the parameters' grid");
  }
  FieldSet missing;
  provided.for_each([&](FieldOrLayer item) {
    if (!item.is_layer && !inputs.initial->has(static_cast<core::FieldId>(item.index))) {
      missing.add(item);
    }
  });
  if (!missing.empty()) {
    return core::make_status(StatusCode::invalid_argument,
                             "the initial state lacks provided fields: " + to_string(missing));
  }
  return {};
}

// R8: a rejected run gets one repair. ok from the hook counts as repaired; the check of the
// rejection is kept if the hook names none. Metric values of run and repair are merged.
StageResult repair_or_keep(Stage& stage, StageContext& ctx, StageResult rejected) {
  StageResult result = stage.repair(ctx, rejected);
  if (result.kind == Kind::ok) {
    result.kind = Kind::repaired;
  }
  if (result.kind == Kind::repaired && result.check_id.empty()) {
    result.check_id = rejected.check_id;
    result.measured = rejected.measured;
    result.required = rejected.required;
  }
  rejected.metrics.values.insert(rejected.metrics.values.end(), result.metrics.values.begin(),
                                 result.metrics.values.end());
  result.metrics.values = std::move(rejected.metrics.values);
  return result;
}

// ok, or internal naming the declared output fields the stage did not write.
core::Status check_outputs(const Stage& stage, const core::WorldState& state) {
  FieldSet missing;
  stage.outputs().for_each([&](FieldOrLayer item) {
    if (!item.is_layer && !state.has(static_cast<core::FieldId>(item.index))) {
      missing.add(item);
    }
  });
  if (missing.empty()) {
    return {};
  }
  return core::make_status(
      StatusCode::internal,
      "stage " + std::string(stage.id()) + " did not write its outputs " + to_string(missing));
}

struct RunArgs {
  const Profile& profile;
  std::span<const std::unique_ptr<Stage>> stages;
  const FieldSet& provided;
  const params::GenParams& params;
  std::uint64_t seed;
  const RunContext& ctx;
  const RunInputs& inputs;
};

RunResult run_attempts(const RunArgs& args, RunReport& report) {
  const RunContext& ctx = args.ctx;
  core::Provenance& prov = report.provenance;
  prov.profile = args.profile.name;
  prov.seed = args.seed;
  prov.params_hash = params::params_hash(args.params);
  prov.library_version = core::version();

  const std::int32_t max_attempts = args.params.checks.max_attempts;
  if (max_attempts < 1) {
    return failure(StatusCode::invalid_argument,
                   "checks.max_attempts is " + std::to_string(max_attempts) + ", must be >= 1",
                   report);
  }
  const core::Grid grid{
      .n = args.params.grid.size, .dx_m = args.params.grid.dx_m, .hu_m = args.params.grid.hu_m};
  if (core::Status s = check_inputs(args.provided, grid, args.inputs); !s.ok()) {
    return failure(std::move(s), report);
  }
  core::MemoryResource& memory =
      ctx.memory != nullptr ? *ctx.memory : core::default_memory_resource();
  const std::size_t stage_count = args.stages.size();

  for (std::uint32_t a = 0; a < static_cast<std::uint32_t>(max_attempts); ++a) {
    prov.attempt = a;
    prov.completed_stages.clear();
    core::Expected<core::WorldState> created = core::WorldState::create(grid, memory);
    if (!created) {
      return failure(std::move(created.error()), report);
    }
    core::WorldState state = std::move(*created);
    if (args.inputs.initial != nullptr) {
      if (core::Status s = copy_initial(*args.inputs.initial, state); !s.ok()) {
        return failure(std::move(s), report);
      }
    }
    ctx.progress.report(kProgressScope, 0.0);

    std::optional<core::RejectionRecord> rejection;
    for (std::size_t k = 0; k < stage_count && !rejection; ++k) {
      Stage& stage = *args.stages[k];
      const std::string id(stage.id());
      if (ctx.cancel.requested()) {
        return failure(StatusCode::cancelled,
                       "cancelled before stage " + id + " of attempt " + std::to_string(a), report);
      }
      StageContext sc{.state = state,
                      .params = args.params,
                      .tables = args.inputs.tables,
                      .key = core::stage_key(args.seed, id, a),
                      .attempt = a,
                      .exec = ctx.exec,
                      .log = ctx.log,
                      .progress = ctx.progress,
                      .cancel = ctx.cancel};
      const auto start = std::chrono::steady_clock::now();
      StageResult result = stage.run(sc);
      if (result.kind == Kind::reject) {
        result = repair_or_keep(stage, sc, std::move(result));
      }
      result.metrics.wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
      result.metrics.bytes_owned_after = state.bytes_owned();
      report.stages.push_back(StageRecord{.stage = id,
                                          .attempt = a,
                                          .kind = result.kind,
                                          .check_id = result.check_id,
                                          .measured = result.measured,
                                          .required = result.required,
                                          .metrics = result.metrics});

      switch (result.kind) {
        case Kind::ok:
        case Kind::repaired: {
          if (core::Status s = check_outputs(stage, state); !s.ok()) {
            return failure(std::move(s), report);
          }
          if (result.kind == Kind::repaired) {
            write_log(ctx.log, core::LogLevel::warning,
                      "attempt " + std::to_string(a) + ": stage " + id + " repaired " +
                          result.check_id + " (measured " + num(result.measured) + ", required " +
                          num(result.required) + ")");
          }
          write_log(ctx.log, core::LogLevel::debug,
                    "attempt " + std::to_string(a) + ": stage " + id + " done in " +
                        num(static_cast<double>(result.metrics.wall_ns) * 1e-6) + " ms");
          prov.completed_stages.push_back(id);
          state.provenance() = prov;
          if (ctx.snapshot != nullptr) {
            const SnapshotInfo info{.profile = args.profile.name,
                                    .stage = id,
                                    .stage_index = k,
                                    .attempt = a,
                                    .final = k + 1 == stage_count};
            if (core::Status s = ctx.snapshot->on_stage(state, info); !s.ok()) {
              s.message = "snapshot after stage " + id + ": " + s.message;
              return failure(std::move(s), report);
            }
          }
          ctx.progress.report(kProgressScope,
                              static_cast<double>(k + 1) / static_cast<double>(stage_count));
          break;
        }
        case Kind::reject:
          rejection = core::RejectionRecord{.attempt = a,
                                            .stage = id,
                                            .check_id = result.check_id,
                                            .measured = result.measured,
                                            .required = result.required};
          write_log(ctx.log, core::LogLevel::warning,
                    "attempt " + std::to_string(a) + ": stage " + id + " rejected " +
                        result.check_id + " (measured " + num(result.measured) + ", required " +
                        num(result.required) + ")");
          break;
        case Kind::cancelled:
          if (result.status.ok()) {
            result.status = core::make_status(StatusCode::cancelled, "stage " + id + " cancelled");
          }
          return failure(std::move(result.status), report);
        case Kind::error:
          if (result.status.ok()) {
            return failure(StatusCode::internal, "stage " + id + " failed without a status",
                           report);
          }
          result.status.message = "stage " + id + ": " + result.status.message;
          return failure(std::move(result.status), report);
      }
    }
    if (!rejection) {
      state.provenance() = prov;
      return RunOutput{.state = std::move(state), .report = std::move(report)};
    }
    prov.rejections.push_back(std::move(*rejection));
  }

  const core::RejectionRecord& last = prov.rejections.back();
  return failure(StatusCode::failed_precondition,
                 "all " + std::to_string(max_attempts) + " attempts were rejected; last: stage " +
                     last.stage + ", check " + last.check_id + " (measured " + num(last.measured) +
                     ", required " + num(last.required) + ")",
                 report);
}

}  // namespace

const core::CancelFlag& never_cancelled() noexcept {
  static const core::CancelFlag flag;
  return flag;
}

Pipeline::Pipeline() noexcept = default;
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;
Pipeline::~Pipeline() = default;

core::Expected<Pipeline> Pipeline::create(const Profile& profile, const StageRegistry& registry,
                                          const FieldSet& provided) noexcept {
  try {
    const std::string prefix = "profile " + profile.name + ": ";
    if (profile.stages.empty()) {
      return core::make_error(StatusCode::invalid_argument, prefix + "no stages");
    }
    Pipeline p;
    p.profile_ = profile;
    p.provided_ = provided;
    p.stages_.reserve(profile.stages.size());
    FieldSet available = provided;
    for (auto it = profile.stages.begin(); it != profile.stages.end(); ++it) {
      const std::string& id = *it;
      if (std::find(profile.stages.begin(), it, id) != it) {
        std::string message = prefix;
        message += "stage " + id + " is listed twice";
        return core::make_error(StatusCode::invalid_argument, message);
      }
      core::Expected<std::unique_ptr<Stage>> stage = registry.create(id);
      if (!stage) {
        stage.error().message = prefix + stage.error().message;
        return core::make_error(std::move(stage.error()));
      }
      const FieldSet missing = (*stage)->inputs().minus(available);
      if (!missing.empty()) {
        std::string message = prefix;
        message += "stage " + id;
        message += " reads " + to_string(missing);
        message += ", which no earlier stage produces";
        return core::make_error(StatusCode::failed_precondition, message);
      }
      available |= (*stage)->outputs();
      p.stages_.push_back(std::move(*stage));
    }
    return {std::move(p)};
  } catch (...) {
    return core::make_error(StatusCode::out_of_memory, "cannot create the pipeline");
  }
}

FieldSet Pipeline::outputs() const noexcept {
  FieldSet all;
  for (const std::unique_ptr<Stage>& stage : stages_) {
    all |= stage->outputs();
  }
  return all;
}

RunResult Pipeline::run(const params::GenParams& params, std::uint64_t seed, const RunContext& ctx,
                        const RunInputs& inputs) noexcept {
  RunReport report;
  try {
    return run_attempts(RunArgs{.profile = profile_,
                                .stages = stages_,
                                .provided = provided_,
                                .params = params,
                                .seed = seed,
                                .ctx = ctx,
                                .inputs = inputs},
                        report);
  } catch (const std::bad_alloc&) {
    return failure(StatusCode::out_of_memory, "pipeline run: out of memory", report);
  } catch (const std::exception& e) {
    return failure(StatusCode::internal, std::string("pipeline run failed: ") + e.what(), report);
  }
}

}  // namespace wg::pipeline
