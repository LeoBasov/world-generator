#pragma once

#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::terrain {

/// Target hypsometric curve G (R4.1): a piecewise-linear CDF through the knots (z_k, cdf_k),
/// with z strictly ascending and cdf non-decreasing from 0 to 1. G is 0 below the first knot
/// and 1 above the last.
struct Hypsometry {
  std::span<const double> z;
  std::span<const double> cdf;
};

/// ok, or invalid_argument unless there are at least two knots, z and cdf have the same size,
/// every value is finite, z rises strictly, and cdf rises weakly from exactly 0 to exactly 1.
[[nodiscard]] core::Status validate(const Hypsometry& h) noexcept;

/// G(z) of a valid curve.
[[nodiscard]] double hypsometry_cdf(const Hypsometry& h, double z) noexcept;

/// G^-1(p) of a valid curve for p in [0, 1]: the smallest z with G(z) >= p, so a flat stretch
/// of G (an elevation range without area) is jumped over.
[[nodiscard]] double hypsometry_quantile(const Hypsometry& h, double p) noexcept;

/// Hypsometric remap of stage C3 (R4.1): z_i <- G^-1(F(z_i)) with F the empirical CDF of z
/// (num::empirical_cdf: F_i = #{j : z_j <= z_i} / m). The map is monotone, so the spatial
/// pattern and the rank order are kept; equal values stay equal, so the clamp atom at the top
/// of the scale maps to the last knot. Errors: invalid_argument for an invalid curve, an empty
/// span or a NaN value, cancelled, out_of_memory.
[[nodiscard]] core::Status hypsometric_remap(
    std::span<float> z, const Hypsometry& h, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::terrain
