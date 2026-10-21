#include "wg/numerics/variogram.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "common.hpp"

namespace wg::num {

core::Expected<std::vector<VariogramPoint>> directional_variogram(
    std::span<const float> z, std::span<const std::uint8_t> mask, const core::Grid& grid,
    const VariogramParams& params, core::Executor& exec) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(z, grid, "directional_variogram: z must have one value per cell"),
       detail::check_optional_field(mask, grid,
                                    "directional_variogram: mask must be empty or per cell")});
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const std::int32_t n = grid.n;
  if ((params.step_x == 0 && params.step_y == 0) || params.step_x < -n || params.step_x > n ||
      params.step_y < -n || params.step_y > n) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "directional_variogram: the step must be non-zero and within n");
  }
  if (params.max_lag < 1 || params.max_lag > n) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "directional_variogram: max_lag must lie in [1, n]");
  }
  const auto lags = static_cast<std::size_t>(params.max_lag);
  const auto chunks = static_cast<std::size_t>(core::chunk_count(0, n, detail::kRowGrain));
  std::vector<double> sums;
  std::vector<std::int64_t> pairs;
  std::vector<VariogramPoint> result;
  try {
    sums.assign(chunks * lags, 0.0);
    pairs.assign(chunks * lags, 0);
    result.resize(lags);
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "directional_variogram: no memory");
  }
  const auto body = [&](const core::ChunkRange& r) {
    const std::size_t base = static_cast<std::size_t>(r.index) * lags;
    for (std::int64_t yy = r.begin; yy < r.end; ++yy) {
      const auto y = static_cast<std::int32_t>(yy);
      for (std::int32_t x = 0; x < n; ++x) {
        const std::int64_t i = grid.idx(x, y);
        if (!detail::in_mask(mask, i)) {
          continue;
        }
        const double zi = z[static_cast<std::size_t>(i)];
        for (std::int32_t h = 1; h <= params.max_lag; ++h) {
          const std::int32_t x2 = x + (h * params.step_x);
          const std::int32_t y2 = y + (h * params.step_y);
          if (!grid.contains(x2, y2)) {
            continue;
          }
          const std::int64_t j = grid.idx(x2, y2);
          if (!detail::in_mask(mask, j)) {
            continue;
          }
          const double d = static_cast<double>(z[static_cast<std::size_t>(j)]) - zi;
          const std::size_t k = base + static_cast<std::size_t>(h) - 1;
          sums[k] += d * d;
          ++pairs[k];
        }
      }
    }
  };
  status = core::parallel_for(exec, 0, n, detail::kRowGrain, body);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const double step_norm = std::sqrt((static_cast<double>(params.step_x) * params.step_x) +
                                     (static_cast<double>(params.step_y) * params.step_y));
  for (std::size_t lag = 0; lag < lags; ++lag) {
    double sum = 0.0;
    std::int64_t count = 0;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
      sum += sums[(chunk * lags) + lag];
      count += pairs[(chunk * lags) + lag];
    }
    VariogramPoint& p = result[lag];
    p.lag = static_cast<std::int32_t>(lag + 1);
    p.distance = static_cast<double>(p.lag) * step_norm;
    p.pairs = count;
    p.gamma = count > 0 ? sum / (2.0 * static_cast<double>(count))
                        : std::numeric_limits<double>::quiet_NaN();
  }
  return result;
}

}  // namespace wg::num
