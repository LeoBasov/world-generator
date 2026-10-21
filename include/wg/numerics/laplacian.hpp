#pragma once

#include <cstdint>
#include <span>

#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::num {

// Masked 5-point Laplacian. A mask (empty = every cell) marks the cells that take part; a D4
// neighbour that lies off the grid or outside the mask is replaced by the centre value
// (zero-flux, Neumann boundary), so nothing flows across the mask edge.

/// out_i = sum over the D4 neighbours j of (z_j - z_i), in field units per cell^2; 0 outside the
/// mask. Errors: invalid_argument for an invalid grid or a span without one value per cell.
[[nodiscard]] core::Status laplacian(std::span<const float> z, std::span<const std::uint8_t> mask,
                                     const core::Grid& grid, std::span<float> out,
                                     core::Executor& exec) noexcept;

/// Parameters of laplacian_smooth.
struct SmoothParams {
  double alpha = 0.25;         ///< step weight, in (0, 0.25] for stability
  std::int32_t iterations = 1;  ///< number of Jacobi steps, >= 0
};

/// Explicit diffusion, iterations times z <- z + alpha * laplacian(z) on the mask cells (final
/// smoothing S7, hillslope diffusion). Cells outside the mask keep their values; the sum over
/// the mask is conserved up to rounding. Errors: those of laplacian, invalid_argument for alpha
/// outside (0, 0.25] or negative iterations, out_of_memory for the scratch buffer.
[[nodiscard]] core::Status laplacian_smooth(
    std::span<float> z, std::span<const std::uint8_t> mask, const core::Grid& grid,
    const SmoothParams& params, core::Executor& exec,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::num
