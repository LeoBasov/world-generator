#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"

namespace wg::num {

// Field statistics over a flat span of values (any field, or one row). A mask (empty = every
// value) selects the values that take part. Counts are integers, so the results are the same
// for any executor.

/// Bins of a histogram: bins equal-width bins over [lo, hi].
struct HistogramParams {
  double lo = 0.0;
  double hi = 1.0;
  std::int32_t bins = 10;  ///< in [1, kMaxHistogramBins]
};

/// Most bins a histogram may have.
inline constexpr std::int32_t kMaxHistogramBins = 1 << 20;

/// Result of histogram. Bin b covers [lo + b * w, lo + (b + 1) * w) with w = (hi - lo) / bins;
/// the last bin also holds values equal to hi.
struct Histogram {
  double lo = 0.0;
  double hi = 1.0;
  std::vector<std::int64_t> counts;
  std::int64_t underflow = 0;  ///< values below lo
  std::int64_t overflow = 0;   ///< values above hi
  std::int64_t nan = 0;        ///< NaN values

  /// Number of mask values, binned or not.
  [[nodiscard]] std::int64_t total() const noexcept;
};

/// Histogram of the mask values. Errors: invalid_argument for lo or hi not finite, lo >= hi,
/// bins outside [1, kMaxHistogramBins] or a mask whose size differs from values; out_of_memory;
/// those of core::parallel_for.
[[nodiscard]] core::Expected<Histogram> histogram(std::span<const float> values,
                                                  std::span<const std::uint8_t> mask,
                                                  const HistogramParams& params,
                                                  core::Executor& exec) noexcept;

/// Range of the finite mask values.
struct ValueRange {
  double min = 0.0;
  double max = 0.0;
  std::int64_t count = 0;  ///< number of finite mask values; min and max are 0 if none
};

/// Smallest and largest finite mask value, e.g. to choose histogram bounds. Errors:
/// invalid_argument for a mask whose size differs from values; those of
/// core::parallel_reduce.
[[nodiscard]] core::Expected<ValueRange> value_range(std::span<const float> values,
                                                     std::span<const std::uint8_t> mask,
                                                     core::Executor& exec) noexcept;

}  // namespace wg::num
