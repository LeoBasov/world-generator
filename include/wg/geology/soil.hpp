#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::geology {

// Soil depth of R10.4 (A21): the steady state of soil production against downslope transport,
//   dh/dt = P_0 e^(-h / h_0) + kappa laplacian(z) = 0  =>  h = h_0 ln(P_0 / (kappa C)),
// with the convexity C = -laplacian(z) [1/m]. Recorded in docs/adr/0019-geology-subsurface.md.

/// Soil depth h of one cell [m] for the convexity C = -laplacian(z) [1/m]: h_0 ln(P_0 /
/// (kappa C)) clamped to [0, soil_max_depth]. Cells where transport cannot balance production
/// (C <= 0, kappa = 0) get soil_max_depth; P_0 <= 0 gives 0.
[[nodiscard]] double steady_soil_depth(double convexity_per_m,
                                       const params::GeologyParams& params) noexcept;

/// Soil depth of every cell: steady_soil_depth of the 5-point Laplacian of z (in metres, hu_m
/// per elevation unit, over dx_m), 0 on mountain cells (environment as GeoEnvironment values).
/// Errors: invalid_argument for an invalid grid, a span without one value per cell or negative
/// soil parameters; cancelled.
[[nodiscard]] core::Status compute_soil_depth(std::span<const float> elevation,
                                              std::span<const std::uint8_t> environment,
                                              const core::Grid& grid,
                                              const params::GeologyParams& params,
                                              std::span<float> soil_depth, core::Executor& exec,
                                              const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::geology
