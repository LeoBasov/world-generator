#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::terrain {

/// Parameters of the plains smoothing (R4.4, elevation_post.plains_*).
struct PlainsParams {
  double z_lo = 110.0;           ///< lower edge of the band
  double z_hi = 290.0;           ///< upper edge of the band
  double taper = 10.0;           ///< width outside the band over which w falls to 0, >= 0
  double lambda = 0.5;           ///< relaxation factor, in [0, 1]
  std::int32_t iterations = 20;  ///< number of steps, >= 0
};

/// Weight w(z) of R4.4: 1 inside [z_lo, z_hi], falling linearly to 0 at z_lo - taper and
/// z_hi + taper, 0 beyond (and outside the band when taper = 0).
[[nodiscard]] double band_weight(double z, const PlainsParams& p) noexcept;

/// Plains by mid-level smoothing (R4.4, stage C5): iterations Jacobi steps
/// z_i <- z_i + lambda w(z_i) (mean of the 8 neighbours - z_i), with w evaluated on the values
/// before the step. A neighbour off the grid counts as the centre value (zero flux, ADR 0004).
/// A cell whose weight is 0 is copied, so a cell that starts outside [z_lo - taper,
/// z_hi + taper] keeps its value bit for bit. Errors: invalid_argument for an invalid grid, a
/// span without one value per cell or bad parameters, cancelled, out_of_memory.
[[nodiscard]] core::Status plains_smooth(
    std::span<float> z, const core::Grid& grid, const PlainsParams& params, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::terrain
