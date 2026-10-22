#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/provenance.hpp"
#include "wg/core/sinks.hpp"
#include "wg/core/status.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/params/tables.hpp"
#include "wg/pipeline/field_set.hpp"
#include "wg/pipeline/profile.hpp"
#include "wg/pipeline/registry.hpp"
#include "wg/pipeline/snapshot.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::pipeline {

/// Cancel flag that is never requested, the default of RunContext::cancel.
const core::CancelFlag& never_cancelled() noexcept;

/// Injected services of a run (plan sections 6.4 and 6.6). The caller owns every object; they
/// must outlive the run.
struct RunContext {
  core::Executor& exec;
  core::LogSink& log = core::null_log_sink();
  core::ProgressSink& progress = core::null_progress_sink();
  /// Polled before every stage; stages pass it to their parallel loops.
  const core::CancelFlag& cancel = never_cancelled();
  /// Called after every completed stage if set.
  SnapshotHook* snapshot = nullptr;
  /// Source of the field storage; nullptr for core::default_memory_resource().
  core::MemoryResource* memory = nullptr;
};

/// Optional inputs of a run beyond parameters and seed.
struct RunInputs {
  /// Data tables for the stages that need them (biomes, geology).
  const params::DataTables* tables = nullptr;
  /// Fields and layers every attempt starts from, e.g. synthetic inputs for testing one stage
  /// in isolation (R4). Its grid must equal the parameters' grid, and it must hold every field
  /// the pipeline was created with as provided. Copied, never modified.
  const core::WorldState* initial = nullptr;
};

/// What one stage run produced, kept for every stage of every attempt.
struct StageRecord {
  std::string stage;
  std::uint32_t attempt = 0;
  StageResult::Kind kind = StageResult::Kind::ok;
  /// Check that failed or was repaired; empty otherwise.
  std::string check_id;
  double measured = 0.0;
  double required = 0.0;
  StageMetrics metrics;
};

/// Account of a run: the provenance of the last attempt (with every rejection) and the records
/// of all stage runs in order.
struct RunReport {
  core::Provenance provenance;
  std::vector<StageRecord> stages;
};

/// A finished world and how it was made. state.provenance() equals report.provenance.
struct RunOutput {
  core::WorldState state;
  RunReport report;
};

/// Why a run produced no world (plan section 6.6: a plain struct for the binding). status.code
/// is cancelled, failed_precondition when every attempt was rejected, invalid_argument for
/// unusable parameters or inputs, or the code of the stage error or snapshot hook failure.
struct FailureReport {
  core::Status status;
  RunReport report;
};

/// Result of Pipeline::run.
using RunResult = core::Expected<RunOutput, FailureReport>;

/// The stages of a profile, checked for causal order, and the runner of plan section 6.4.
class Pipeline {
 public:
  /// Instantiates the profile's stages from the registry and checks R2: every stage reads only
  /// fields and layers in provided or written by an earlier stage,
  /// in(s_k) subset of provided + out(s_1) + ... + out(s_{k-1}). provided is F_0: empty for a
  /// full profile (the grid geometry is no field), the synthetic inputs when stages run
  /// without their upstream stages. Errors: invalid_argument for an empty profile or a stage
  /// listed twice (its id keys the RNG), not_found for an unregistered stage,
  /// failed_precondition naming the stage and the fields no earlier stage produces, and the
  /// errors of StageRegistry::create.
  [[nodiscard]] static core::Expected<Pipeline> create(const Profile& profile,
                                                       const StageRegistry& registry,
                                                       const FieldSet& provided = {}) noexcept;

  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&) noexcept;
  Pipeline& operator=(Pipeline&&) noexcept;
  ~Pipeline();

  [[nodiscard]] const Profile& profile() const noexcept { return profile_; }
  [[nodiscard]] const FieldSet& provided() const noexcept { return provided_; }
  /// Every field and layer the profile writes.
  [[nodiscard]] FieldSet outputs() const noexcept;

  /// Generates a world. Attempt a = 0, 1, ... params.checks.max_attempts - 1 starts from a
  /// fresh WorldState on the parameters' grid (plus the initial fields) and runs the stages in
  /// order with key stage_key(seed, id, a) and the same parameters. A stage that rejects and
  /// cannot repair adds a RejectionRecord and starts the next attempt; the first attempt whose
  /// stages all succeed is returned. Cancel is polled before every stage. Progress goes to
  /// scope "pipeline" as completed stages / stage count, restarting at 0 with every attempt;
  /// warnings for repairs and rejections go to the log. One run at a time per Pipeline; runs of
  /// different Pipelines share no mutable state.
  [[nodiscard]] RunResult run(const params::GenParams& params, std::uint64_t seed,
                              const RunContext& ctx, const RunInputs& inputs = {}) noexcept;

 private:
  Pipeline() noexcept;

  Profile profile_;
  FieldSet provided_;
  std::vector<std::unique_ptr<Stage>> stages_;
};

}  // namespace wg::pipeline
