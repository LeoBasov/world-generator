#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/sinks.hpp"
#include "wg/core/status.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/params/tables.hpp"
#include "wg/pipeline/field_set.hpp"

namespace wg::pipeline {

/// Measurements of one stage run (plan section 9.1, A08). Diagnostics only: wall time varies
/// between runs, so metrics never enter a hash or a world file's field data.
struct StageMetrics {
  /// Wall time of run (and repair, if called) [ns]; set by the runner.
  std::int64_t wall_ns = 0;
  /// WorldState::bytes_owned() after the stage; set by the runner.
  std::size_t bytes_owned_after = 0;
  /// Values the stage reports itself, e.g. {"iterations", 250}, in the order added.
  std::vector<std::pair<std::string, double>> values;

  void add(std::string_view name, double value) noexcept { values.emplace_back(name, value); }

  /// Value added under name (the first one), or nullopt.
  [[nodiscard]] std::optional<double> value(std::string_view name) const noexcept {
    for (const auto& [key, v] : values) {
      if (key == name) {
        return v;
      }
    }
    return std::nullopt;
  }
};

/// Outcome of a stage run (plan section 6.4).
struct StageResult {
  enum class Kind : std::uint8_t {
    ok,         ///< outputs written, every check passed
    repaired,   ///< a check failed and was repaired (R8: repair before rejection)
    reject,     ///< a check failed and cannot be repaired: the attempt restarts
    cancelled,  ///< the cancel flag was observed
    error,      ///< the stage cannot run (status says why); the run stops
  };

  Kind kind = Kind::ok;
  /// R11 check that failed or was repaired, e.g. "PEAK_NUMBER_MIN"; empty for ok.
  std::string check_id;
  double measured = 0.0;
  double required = 0.0;
  /// Why the stage failed, for error (and optionally cancelled); ok otherwise.
  core::Status status;
  StageMetrics metrics;

  [[nodiscard]] static StageResult ok() noexcept { return {}; }
  [[nodiscard]] static StageResult repaired(std::string_view check_id, double measured,
                                            double required) noexcept {
    return with_check(Kind::repaired, check_id, measured, required);
  }
  [[nodiscard]] static StageResult reject(std::string_view check_id, double measured,
                                          double required) noexcept {
    return with_check(Kind::reject, check_id, measured, required);
  }
  [[nodiscard]] static StageResult cancelled() noexcept {
    StageResult r;
    r.kind = Kind::cancelled;
    return r;
  }
  [[nodiscard]] static StageResult error(core::Status status) noexcept {
    StageResult r;
    r.kind = Kind::error;
    r.status = std::move(status);
    return r;
  }

 private:
  static StageResult with_check(Kind kind, std::string_view check_id, double measured,
                                double required) noexcept {
    StageResult r;
    r.kind = kind;
    r.check_id = std::string(check_id);
    r.measured = measured;
    r.required = required;
    return r;
  }
};

/// Name of a result kind, e.g. "reject".
std::string_view to_string(StageResult::Kind kind) noexcept;

/// Everything a stage sees during one run. Bundles the arguments of the plan 6.4 sketch
/// (state, parameters, key, run context) with the data tables, so the signature of
/// Stage::run stays stable as stages need more (ADR 0006).
struct StageContext {
  /// The world being built. A stage reads only its inputs and writes only its outputs.
  core::WorldState& state;
  const params::GenParams& params;
  /// Data tables (biomes, geology, ...), or nullptr if the run was given none.
  const params::DataTables* tables;
  /// sigma_{s,a} = stage_key(seed, id, attempt): the only source of randomness (R1).
  core::RngKey key;
  std::uint32_t attempt;
  core::Executor& exec;
  core::LogSink& log;
  /// Progress of the stage under its own id as scope.
  core::ProgressSink& progress;
  /// Pass to parallel_for; long sequential loops poll it themselves.
  const core::CancelFlag& cancel;
};

/// One step of a profile (plan sections 2 and 6.4). A stage communicates only through the
/// fields and layers of the WorldState: it reads its inputs, writes its outputs and calls
/// kernels for the numerical work. It holds no state between runs, so one instance can run in
/// every attempt.
class Stage {
 public:
  virtual ~Stage() = default;

  /// Stable id, equal to the source file stem (e.g. "c02_seed"); keys the RNG.
  [[nodiscard]] virtual std::string_view id() const noexcept = 0;
  /// Fields and layers the stage reads (plan's in(s); "requires" is a C++20 keyword).
  [[nodiscard]] virtual FieldSet inputs() const noexcept = 0;
  /// Fields and layers the stage writes. The runner checks that every output field exists
  /// after an ok or repaired run.
  [[nodiscard]] virtual FieldSet outputs() const noexcept = 0;

  /// Runs the stage on ctx.state.
  virtual StageResult run(StageContext& ctx) noexcept = 0;

  /// Repair hook (R8). Called once by the runner when run returned reject, with that result
  /// and the state as run left it. Returns repaired (or ok) if the failure was fixed in place,
  /// so the attempt continues; anything else stands. The default repairs nothing.
  virtual StageResult repair(StageContext& ctx, const StageResult& rejected) noexcept {
    static_cast<void>(ctx);
    return rejected;
  }

 protected:
  Stage() = default;
  Stage(const Stage&) = default;
  Stage& operator=(const Stage&) = default;
  Stage(Stage&&) = default;
  Stage& operator=(Stage&&) = default;
};

}  // namespace wg::pipeline
