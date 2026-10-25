#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "support/source_files.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/sinks.hpp"
#include "wg/pipeline/pipeline.hpp"
#include "wg/stages/classic.hpp"

namespace wg::test {

// Runs single library stages outside a Pipeline, as Pipeline::run would for attempt 0.

/// Registry of the classic stages.
inline wg::pipeline::StageRegistry classic_registry() {
  wg::pipeline::StageRegistry registry;
  REQUIRE(wg::stages::add_classic_stages(registry).ok());
  return registry;
}

/// New instance of classic stage id.
inline std::unique_ptr<wg::pipeline::Stage> make_classic_stage(std::string_view id) {
  auto stage = classic_registry().create(id);
  REQUIRE(stage);
  return std::move(*stage);
}

/// Calls f(stage, ctx) with the context Pipeline::run gives stage on state in attempt 0 (key
/// stage_key(seed, id, 0)), with the data tables tables.
template <class F>
wg::pipeline::StageResult with_context(wg::pipeline::Stage& stage, wg::core::WorldState& state,
                                       const wg::params::GenParams& params, std::uint64_t seed,
                                       wg::core::Executor& exec, const wg::core::CancelFlag& cancel,
                                       const wg::params::DataTables* tables, const F& f) {
  wg::pipeline::StageContext ctx{.state = state,
                                 .params = params,
                                 .tables = tables,
                                 .key = wg::core::stage_key(seed, stage.id(), 0),
                                 .attempt = 0,
                                 .exec = exec,
                                 .log = wg::core::null_log_sink(),
                                 .progress = wg::core::null_progress_sink(),
                                 .cancel = cancel};
  return f(stage, ctx);
}

/// The data tables of data/ (loaded once).
inline const wg::params::DataTables& repo_tables() {
  static const wg::params::DataTables tables = [] {
    auto loaded = wg::params::load_tables_dir(source_path("data"));
    REQUIRE(loaded);
    return std::move(*loaded);
  }();
  return tables;
}

/// Runs stage on state with key stage_key(seed, id, 0) and the data tables tables (none by
/// default).
inline wg::pipeline::StageResult run_stage(
    wg::pipeline::Stage& stage, wg::core::WorldState& state, const wg::params::GenParams& params,
    std::uint64_t seed, wg::core::Executor& exec,
    const wg::core::CancelFlag& cancel = wg::pipeline::never_cancelled(),
    const wg::params::DataTables* tables = nullptr) {
  return with_context(
      stage, state, params, seed, exec, cancel, tables,
      [](wg::pipeline::Stage& s, wg::pipeline::StageContext& ctx) { return s.run(ctx); });
}

/// Like run_stage, then like the runner (R8): a reject gets one call of the repair hook, whose
/// result is returned.
inline wg::pipeline::StageResult run_stage_with_repair(wg::pipeline::Stage& stage,
                                                       wg::core::WorldState& state,
                                                       const wg::params::GenParams& params,
                                                       std::uint64_t seed,
                                                       wg::core::Executor& exec) {
  return with_context(stage, state, params, seed, exec, wg::pipeline::never_cancelled(), nullptr,
                      [](wg::pipeline::Stage& s, wg::pipeline::StageContext& ctx) {
                        wg::pipeline::StageResult r = s.run(ctx);
                        if (r.kind == wg::pipeline::StageResult::Kind::reject) {
                          r = s.repair(ctx, r);
                        }
                        return r;
                      });
}

/// Final state of a pipeline run of the classic stages stages with the data tables of data/.
inline wg::core::WorldState run_classic(const std::vector<std::string>& stages,
                                        const wg::params::GenParams& params, std::uint64_t seed,
                                        wg::core::Executor& exec) {
  const wg::pipeline::StageRegistry registry = classic_registry();
  const wg::pipeline::Profile profile{.name = "test", .stages = stages};
  auto pipeline = wg::pipeline::Pipeline::create(profile, registry);
  REQUIRE(pipeline);
  auto out = pipeline->run(params, seed, wg::pipeline::RunContext{.exec = exec},
                           wg::pipeline::RunInputs{.tables = &repo_tables(), .initial = nullptr});
  REQUIRE(out);
  return std::move(out->state);
}

}  // namespace wg::test
