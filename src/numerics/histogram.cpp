#include "wg/numerics/histogram.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>

#include "common.hpp"

namespace wg::num {

namespace {

core::Status check_mask(std::span<const float> values, std::span<const std::uint8_t> mask) {
  if (!mask.empty() && mask.size() != values.size()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "histogram: mask must be empty or have one entry per value");
  }
  return {};
}

}  // namespace

std::int64_t Histogram::total() const noexcept {
  return std::accumulate(counts.begin(), counts.end(), std::int64_t{0}) + underflow + overflow +
         nan;
}

core::Expected<Histogram> histogram(std::span<const float> values,
                                    std::span<const std::uint8_t> mask,
                                    const HistogramParams& params, core::Executor& exec) noexcept {
  core::Status status = check_mask(values, mask);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  if (!std::isfinite(params.lo) || !std::isfinite(params.hi) || !(params.lo < params.hi)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "histogram: lo and hi must be finite with lo < hi");
  }
  if (params.bins < 1 || params.bins > kMaxHistogramBins) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "histogram: bins outside [1, kMaxHistogramBins]");
  }
  const auto size = static_cast<std::int64_t>(values.size());
  const auto bins = static_cast<std::size_t>(params.bins);
  // Per chunk: bins counts, then underflow, overflow and NaN.
  const std::size_t stride = bins + 3;
  const auto chunks = static_cast<std::size_t>(core::chunk_count(0, size, detail::kValueGrain));
  Histogram result;
  std::vector<std::int64_t> partials;
  try {
    result.counts.assign(bins, 0);
    partials.assign(chunks * stride, 0);
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "histogram: no memory");
  }
  result.lo = params.lo;
  result.hi = params.hi;
  const double scale = static_cast<double>(params.bins) / (params.hi - params.lo);
  const auto body = [&](const core::ChunkRange& r) {
    const std::span<std::int64_t> c =
        std::span<std::int64_t>(partials).subspan(static_cast<std::size_t>(r.index) * stride,
                                                  stride);
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      if (!detail::in_mask(mask, i)) {
        continue;
      }
      const double v = values[static_cast<std::size_t>(i)];
      if (std::isnan(v)) {
        ++c[bins + 2];
      } else if (v < params.lo) {
        ++c[bins];
      } else if (v > params.hi) {
        ++c[bins + 1];
      } else {
        const auto b = static_cast<std::size_t>((v - params.lo) * scale);
        ++c[std::min(b, bins - 1)];
      }
    }
  };
  status = core::parallel_for(exec, 0, size, detail::kValueGrain, body);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
    const std::size_t base = chunk * stride;
    for (std::size_t b = 0; b < bins; ++b) {
      result.counts[b] += partials[base + b];
    }
    result.underflow += partials[base + bins];
    result.overflow += partials[base + bins + 1];
    result.nan += partials[base + bins + 2];
  }
  return result;
}

core::Expected<ValueRange> value_range(std::span<const float> values,
                                       std::span<const std::uint8_t> mask,
                                       core::Executor& exec) noexcept {
  core::Status status = check_mask(values, mask);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const auto map = [&](const core::ChunkRange& r) {
    ValueRange range;
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      const double v = values[static_cast<std::size_t>(i)];
      if (!detail::in_mask(mask, i) || !std::isfinite(v)) {
        continue;
      }
      range.min = range.count == 0 ? v : std::min(range.min, v);
      range.max = range.count == 0 ? v : std::max(range.max, v);
      ++range.count;
    }
    return range;
  };
  const auto combine = [](ValueRange a, ValueRange b) {
    if (a.count == 0) {
      return b;
    }
    if (b.count == 0) {
      return a;
    }
    return ValueRange{.min = std::min(a.min, b.min),
                      .max = std::max(a.max, b.max),
                      .count = a.count + b.count};
  };
  const auto size = static_cast<std::int64_t>(values.size());
  return core::parallel_reduce(exec, 0, size, detail::kValueGrain, ValueRange{}, map, combine);
}

}  // namespace wg::num
