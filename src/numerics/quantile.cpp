#include "wg/numerics/quantile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "common.hpp"
#include "wg/core/arena.hpp"

namespace wg::num {

namespace {

// Type 7 quantile of a non-empty ascending range, p in [0, 1].
double interpolate(std::span<const double> sorted, double p) {
  const double h = p * static_cast<double>(sorted.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(h));
  if (lo + 1 >= sorted.size()) {
    return sorted.back();
  }
  return sorted[lo] + ((h - static_cast<double>(lo)) * (sorted[lo + 1] - sorted[lo]));
}

// Calls emit(i, F(v_i)) for every mask value i, in ascending (value, index) order.
template <class Emit>
core::Status for_each_cdf(std::span<const float> values, std::span<const std::uint8_t> mask,
                          std::size_t out_size, core::MemoryResource& resource,
                          const Emit& emit) {
  if (!mask.empty() && mask.size() != values.size()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "empirical_cdf: mask must be empty or have one entry per value");
  }
  if (out_size != values.size()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "empirical_cdf: out must have one entry per value");
  }
  if (values.empty()) {
    return {};
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<std::int64_t>(values.size()), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::int64_t>> storage = arena->allocate<std::int64_t>(values.size());
  if (!storage) {
    return std::move(storage.error());
  }
  const std::span<std::int64_t> order = *storage;
  std::size_t m = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (!detail::in_mask(mask, static_cast<std::int64_t>(i))) {
      continue;
    }
    if (std::isnan(values[i])) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "empirical_cdf: NaN value in the mask");
    }
    order[m++] = static_cast<std::int64_t>(i);
  }
  const auto value = [&values](std::int64_t i) { return values[static_cast<std::size_t>(i)]; };
  const std::span<std::int64_t> ranked = order.first(m);
  std::sort(ranked.begin(), ranked.end(), [&value](std::int64_t a, std::int64_t b) {
    return value(a) < value(b) || (!(value(b) < value(a)) && a < b);
  });
  const auto total = static_cast<double>(m);
  std::size_t begin = 0;
  while (begin < m) {
    std::size_t end = begin + 1;
    while (end < m && !(value(ranked[begin]) < value(ranked[end]))) {
      ++end;
    }
    const double f = static_cast<double>(end) / total;
    for (std::size_t t = begin; t < end; ++t) {
      emit(ranked[t], f);
    }
    begin = end;
  }
  return {};
}

}  // namespace

core::Status empirical_cdf(std::span<const float> values, std::span<const std::uint8_t> mask,
                           std::span<float> out, core::MemoryResource& resource) noexcept {
  return for_each_cdf(values, mask, out.size(), resource, [&out](std::int64_t i, double f) {
    out[static_cast<std::size_t>(i)] = static_cast<float>(f);
  });
}

core::Expected<double> quantile(std::span<const double> sorted, double p) noexcept {
  if (sorted.empty()) {
    return core::make_error(core::StatusCode::invalid_argument, "quantile: no values");
  }
  if (!(p >= 0.0 && p <= 1.0)) {
    return core::make_error(core::StatusCode::invalid_argument, "quantile: p outside [0, 1]");
  }
  return interpolate(sorted, p);
}

core::Status quantile_map(std::span<const float> values, std::span<const std::uint8_t> mask,
                          std::span<const double> target_sorted, std::span<float> out,
                          core::MemoryResource& resource) noexcept {
  if (target_sorted.empty() || !std::is_sorted(target_sorted.begin(), target_sorted.end())) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "quantile_map: the target must be non-empty and sorted");
  }
  return for_each_cdf(values, mask, out.size(), resource,
                      [&out, target_sorted](std::int64_t i, double f) {
                        out[static_cast<std::size_t>(i)] =
                            static_cast<float>(interpolate(target_sorted, f));
                      });
}

}  // namespace wg::num
