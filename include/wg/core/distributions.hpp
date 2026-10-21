#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/rng.hpp"

namespace wg::core {

// Sampling on top of the counter RNG (plan section 6.3). There are no std:: distributions,
// because their algorithms differ between libstdc++, libc++ and MSVC. Every function is
// deterministic and uses det_exp / det_log, so the draws are the same bits on every platform.
// Single-draw samplers take (key, cell, draw) like uniform01. Samplers that need a variable
// number of draws take a local RngStream.

/// Phi^-1(p), the standard normal quantile (Wichura 1988, AS 241, relative error about 1e-16).
/// Returns -inf for p <= 0, +inf for p >= 1, NaN for NaN.
double inverse_normal_cdf(double p) noexcept;

/// Normal draw mean + sd * Phi^-1(u) with u = uniform_open01(key, cell, draw). One draw.
double normal(RngKey key, std::uint64_t cell, std::uint64_t draw, double mean, double sd) noexcept;
double normal(RngStream& stream, double mean, double sd) noexcept;

/// Log-normal draw det_exp(normal(key, i, k, mu, sigma)) for cell (or stream id) i and draw k:
/// mu and sigma belong to the logarithm. One draw.
double log_normal(RngKey key, std::uint64_t i, std::uint64_t k, double mu, double sigma) noexcept;
double log_normal(RngStream& stream, double mu, double sigma) noexcept;

/// Largest supported Poisson mean; larger values are clamped to it.
inline constexpr double kMaxPoissonLambda = 1e12;

/// Poisson draw with mean lambda. Below lambda = 10 it uses inversion with one uniform; above
/// that, Hoermann's PTRS transformed rejection (1993), which takes two or more uniforms.
/// lambda <= 0 or NaN gives 0 and takes no draw.
std::uint64_t poisson(RngStream& stream, double lambda) noexcept;

/// Most bins a WeightedBins table holds.
inline constexpr std::size_t kMaxWeightedBins = 32;

/// Discrete distribution over bins 0 ... n-1 with probability proportional to the weights,
/// e.g. the weighted control values of R§3.2. The cumulative weights are summed in index order
/// in float64. Bins of weight 0 are never chosen. The table has a fixed capacity, so it is
/// trivially copyable and sampling never allocates.
class WeightedBins {
 public:
  /// Fails with invalid_argument for no bins, a negative or non-finite weight, or a sum that is
  /// zero or not finite. Fails with out_of_range for more than kMaxWeightedBins bins.
  static Expected<WeightedBins> create(std::span<const double> weights) noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] double total() const noexcept { return size_ == 0 ? 0.0 : cdf_[size_ - 1]; }

  /// Bin for a uniform u in [0, 1): the first bin whose cumulative weight exceeds u * total.
  [[nodiscard]] std::size_t sample(double u) const noexcept;
  /// Bin for uniform01(key, i, k): cell (or stream id) i, draw k. One draw.
  [[nodiscard]] std::size_t sample(RngKey key, std::uint64_t i, std::uint64_t k) const noexcept {
    return sample(uniform01(key, i, k));
  }

 private:
  WeightedBins() = default;

  std::array<double, kMaxWeightedBins> cdf_{};
  std::size_t size_ = 0;
  // Last bin with weight > 0; the result when u * total rounds up to the total.
  std::size_t last_positive_ = 0;
};

}  // namespace wg::core
