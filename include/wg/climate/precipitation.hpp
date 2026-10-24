#pragma once

#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::climate {

// Orographic precipitation of R7.2 (A18): an upslope model with moisture depletion, swept once
// along the prevailing wind from west to east (+x). With i - 1 the upwind neighbour of i:
//   P_i = P_base,i q_{i-1} / q_max + k_o q_{i-1} max(0, z_i - z_{i-1})
//   q_i = clamp(q_{i-1} - eta P_i + E_i, 0, q_max)
// with the cell size as the unit of length (Delta s = 1 tile). The conventions are recorded in
// docs/adr/0016-orographic-precipitation.md.

/// Drainage below which the DF quirk leaves a cell's rainfall unchanged (R7.1).
inline constexpr double kQuirkDrainageMin = 50.0;

/// Per-cell inputs of orographic_precipitation, one value per cell each.
struct PrecipitationInputs {
  std::span<const float> base{};       ///< P_base in [0, 100] (required)
  std::span<const float> elevation{};  ///< z [elevation units] (required)
  std::span<const float> drainage{};   ///< D in [0, 100]; required only with df_drainage_quirk
};

/// Writes the rainfall P of every cell into out. Each row is swept from x = 0 eastwards; the air
/// enters the map saturated (q = q_max) and cell 0 of a row has no upslope term. The slope uses
/// the surface max(z, terrain::kSeaLevel), so the sea floor causes no lift. Ocean cells
/// (z < terrain::kSeaLevel) recharge by recharge_ocean, all other cells by recharge_land. P is
/// clamped to the registry range [0, 100], and the clamped value is drawn from the column. With
/// df_drainage_quirk, cells with D < kQuirkDrainageMin keep P_base exactly but still draw from
/// the column. With orographic_precipitation off, out is a copy of the base and the sweep does
/// not run.
///
/// moisture, if not empty, receives q after each cell (not written when the toggle is off). out
/// may be in.base (in place); no other spans may overlap. Rows are independent, so the result
/// does not depend on the thread count. Errors: invalid_argument for an invalid grid, a span
/// without one value per cell, or no drainage with df_drainage_quirk; those of
/// core::parallel_for, cancelled included.
[[nodiscard]] core::Status orographic_precipitation(
    const core::Grid& grid, const PrecipitationInputs& in, const params::PrecipitationParams& p,
    std::span<float> out, std::span<float> moisture, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::climate
