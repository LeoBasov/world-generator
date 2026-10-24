#pragma once

#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"

namespace wg::hydro {

// Weights of the flow accumulation for stage C6 (A17). accumulate_flow sums them along the
// receivers into the upstream area (field flow_accum) and the discharge (field discharge). The
// conventions are recorded in docs/adr/0015-river-network.md.

/// Seconds of a Julian year, to convert a discharge from m3/yr to m3/s.
inline constexpr double kSecondsPerYear = 31'557'600.0;

/// Area weights of the upstream drainage area: the cell area dx^2 [m2] on land, 0 on ocean
/// (z < terrain::kSeaLevel). Errors: invalid_argument for an invalid grid or spans without
/// one value per cell.
[[nodiscard]] core::Status area_weights(std::span<const float> z, const core::Grid& grid,
                                        std::span<double> weights) noexcept;

/// Runoff weights of the discharge (R6.5): w = r * P_mm / 1000 * dx^2 [m3/yr] on land and lake
/// cells, with the runoff coefficient r = 1 - D / 100 and P_mm = 30 P [mm/yr]; 0 on ocean
/// (z < terrain::kSeaLevel). P and D are clamped to [0, 100]. Errors: invalid_argument for an
/// invalid grid, spans without one value per cell or a non-finite rainfall or drainage.
[[nodiscard]] core::Status runoff_weights(std::span<const float> z, std::span<const float> rainfall,
                                          std::span<const float> drainage, const core::Grid& grid,
                                          std::span<double> weights) noexcept;

}  // namespace wg::hydro
