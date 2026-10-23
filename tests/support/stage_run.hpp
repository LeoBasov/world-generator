#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

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

/// Runs stage on state with key stage_key(seed, id, 0).
inline wg::pipeline::StageResult run_stage(
    wg::pipeline::Stage& stage, wg::core::WorldState& state, const wg::params::GenParams& params,
    std::uint64_t seed, wg::core::Executor& exec,
    const wg::core::CancelFlag& cancel = wg::pipeline::never_cancelled()) {
  wg::pipeline::StageContext ctx{.state = state,
                                 .params = params,
                                 .tables = nullptr,
                                 .key = wg::core::stage_key(seed, stage.id(), 0),
                                 .attempt = 0,
                                 .exec = exec,
                                 .log = wg::core::null_log_sink(),
                                 .progress = wg::core::null_progress_sink(),
                                 .cancel = cancel};
  return stage.run(ctx);
}

}  // namespace wg::test
