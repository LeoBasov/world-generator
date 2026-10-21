#include "wg/numerics/differences.hpp"

#include <cstddef>
#include <cstdint>

#include "common.hpp"
#include "wg/numerics/neighborhood.hpp"

namespace wg::num {

namespace {

// Difference quotient between cells a and b that lie `apart` cells apart.
float quotient(std::span<const float> z, std::int64_t a, std::int64_t b, std::int32_t apart) {
  const double dz = static_cast<double>(z[static_cast<std::size_t>(b)]) -
                    static_cast<double>(z[static_cast<std::size_t>(a)]);
  return static_cast<float>(dz / static_cast<double>(apart));
}

}  // namespace

core::Status central_differences(std::span<const float> z, const core::Grid& grid,
                                 std::span<float> dzdx, std::span<float> dzdy,
                                 core::Executor& exec) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(z, grid, "central_differences: z must have one value per cell"),
       detail::check_field(dzdx, grid, "central_differences: dzdx must have one value per cell"),
       detail::check_field(dzdy, grid, "central_differences: dzdy must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const std::int32_t n = grid.n;
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t yy = r.begin; yy < r.end; ++yy) {
      const auto y = static_cast<std::int32_t>(yy);
      const std::int32_t y0 = y == 0 ? 0 : y - 1;
      const std::int32_t y1 = y == n - 1 ? n - 1 : y + 1;
      for (std::int32_t x = 0; x < n; ++x) {
        const std::int32_t x0 = x == 0 ? 0 : x - 1;
        const std::int32_t x1 = x == n - 1 ? n - 1 : x + 1;
        const auto i = static_cast<std::size_t>(grid.idx(x, y));
        dzdx[i] = quotient(z, grid.idx(x0, y), grid.idx(x1, y), x1 - x0);
        dzdy[i] = quotient(z, grid.idx(x, y0), grid.idx(x, y1), y1 - y0);
      }
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, body);
}

core::Status d8_slope(std::span<const float> z, const core::Grid& grid, std::span<float> slope,
                      std::span<std::uint8_t> dir, core::Executor& exec) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(z, grid, "d8_slope: z must have one value per cell"),
       detail::check_field(slope, grid, "d8_slope: slope must have one value per cell"),
       detail::check_optional_field(dir, grid, "d8_slope: dir must be empty or per cell")});
  if (!status.ok()) {
    return status;
  }
  const double scale = grid.hu_m / grid.dx_m;
  const std::int32_t n = grid.n;
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const double zi = z[static_cast<std::size_t>(i)];
      double best = 0.0;
      std::uint8_t best_dir = kNoDir;
      for_each_neighbor(grid, i, Connectivity::d8, [&](std::int64_t j, core::Dir8 d) {
        const double drop = (zi - static_cast<double>(z[static_cast<std::size_t>(j)])) /
                            step_length(d);
        if (drop > best) {
          best = drop;
          best_dir = static_cast<std::uint8_t>(d);
        }
      });
      slope[static_cast<std::size_t>(i)] = static_cast<float>(best * scale);
      if (!dir.empty()) {
        dir[static_cast<std::size_t>(i)] = best_dir;
      }
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, body);
}

}  // namespace wg::num
