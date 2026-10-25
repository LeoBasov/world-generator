#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/rng.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::hydro {

/// Summary of an erosion run.
struct ErosionReport {
  /// Test-river sources before the first and after the last cycle (RIVER_MINS, R6.1).
  std::int32_t sources_before = 0;
  std::int32_t sources_after = 0;
  /// Cycles run and walker steps taken over all of them.
  std::int32_t cycles = 0;
  std::int64_t walker_steps = 0;
};

/// Test-river erosion of the classic profile (R6.1, R6.3, R6.4). First snaps z (and the dig
/// depth) to the lattice of kErosionQuantum. Then each cycle c of erosion_cycle_count finds the
/// sources (find_river_sources), runs one walker from each in ascending order with the stream
/// core::RngStream(key.derive(c), source) and a failed-basin memo that is cleared at the start
/// of the cycle (run_walker, ADR 0021), and, with periodically_erode_extremes, ends with
/// thermal_erode (talus_threshold, talus_fraction). Cycle c draws the same numbers whatever the
/// cycle count, so a run of n cycles is a prefix of a run of n + 1. Serial, so the result is
/// the same on every platform. stop_mask (empty: none) stops the walkers, e.g. at lakes;
/// cancel, if given, is polled before every cycle. Errors: invalid_argument for an invalid
/// grid, spans without one value per cell, invalid params or values that cannot be snapped (z
/// is unchanged then); cancelled; the errors of thermal_erode; out_of_memory.
[[nodiscard]] core::Expected<ErosionReport> erode(
    std::span<float> z, const core::Grid& grid, const params::ErosionParams& params,
    core::RngKey key, std::span<const std::uint8_t> stop_mask = {},
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::hydro
