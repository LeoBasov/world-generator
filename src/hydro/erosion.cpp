#include "wg/hydro/erosion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/hydro/test_rivers.hpp"
#include "wg/hydro/thermal.hpp"

namespace wg::hydro {

namespace detail = num::detail;

core::Expected<ErosionReport> erode(std::span<float> z, const core::Grid& grid,
                                    const params::ErosionParams& params, core::RngKey key,
                                    std::span<const std::uint8_t> stop_mask,
                                    const core::CancelFlag* cancel,
                                    core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "erode: z must have one value per cell"),
           detail::check_optional_field(stop_mask, grid,
                                        "erode: stop_mask must be empty or have one value per "
                                        "cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (params.erosion_cycle_count < 0 || params.walker_step_limit < 0 ||
      !std::isfinite(params.dig_depth) || params.dig_depth < 0.0 ||
      params.dig_depth >= kLatticeLimit || !std::isfinite(params.talus_threshold) ||
      params.talus_threshold < 0.0 ||
      !(params.talus_fraction >= 0.0 && params.talus_fraction <= 1.0)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "erode: invalid erosion parameters");
  }
  if (core::Status s = snap_to_lattice(z); !s.ok()) {
    return core::make_error(std::move(s));
  }
  const WalkerParams walker{.dig_depth = snap_to_lattice(params.dig_depth),
                            .step_limit = params.walker_step_limit};
  const ThermalParams thermal{.talus_threshold = params.talus_threshold,
                              .fraction = params.talus_fraction,
                              .max_transfers = ThermalParams{}.max_transfers};

  core::Expected<core::Arena> arena =
      core::Arena::create(2 * detail::arena_bytes<std::uint8_t>(z.size()), resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<std::uint8_t>> visited = arena->allocate<std::uint8_t>(z.size());
  if (!visited) {
    return core::make_error(std::move(visited.error()));
  }
  std::fill(visited->begin(), visited->end(), std::uint8_t{0});
  // Cells of floods without an exit in the current cycle (ADR 0021).
  core::Expected<std::span<std::uint8_t>> failed = arena->allocate<std::uint8_t>(z.size());
  if (!failed) {
    return core::make_error(std::move(failed.error()));
  }

  ErosionReport report;
  core::Expected<std::vector<std::int32_t>> sources = find_river_sources(z, grid);
  if (!sources) {
    return core::make_error(std::move(sources.error()));
  }
  report.sources_before = static_cast<std::int32_t>(sources->size());
  for (std::int32_t c = 0; c < params.erosion_cycle_count; ++c) {
    if (cancel != nullptr && cancel->requested()) {
      return core::make_error(core::StatusCode::cancelled, "erode: cancelled");
    }
    if (c > 0) {
      sources = find_river_sources(z, grid);
      if (!sources) {
        return core::make_error(std::move(sources.error()));
      }
    }
    const core::RngKey cycle_key = key.derive(static_cast<std::uint64_t>(c));
    std::fill(failed->begin(), failed->end(), std::uint8_t{0});
    for (const std::int32_t source : *sources) {
      core::RngStream stream(cycle_key, static_cast<std::uint64_t>(source));
      core::Expected<WalkerResult> walk =
          run_walker(z, grid, source, walker, stream, *visited, stop_mask, nullptr, *failed);
      if (!walk) {
        return core::make_error(std::move(walk.error()));
      }
      report.walker_steps += walk->steps;
    }
    if (params.periodically_erode_extremes) {
      core::Expected<std::int64_t> transfers = thermal_erode(z, grid, thermal, resource);
      if (!transfers) {
        return core::make_error(std::move(transfers.error()));
      }
    }
    ++report.cycles;
  }
  if (params.erosion_cycle_count > 0) {
    sources = find_river_sources(z, grid);
    if (!sources) {
      return core::make_error(std::move(sources.error()));
    }
  }
  report.sources_after = static_cast<std::int32_t>(sources->size());
  return report;
}

}  // namespace wg::hydro
