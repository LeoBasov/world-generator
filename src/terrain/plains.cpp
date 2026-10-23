#include "wg/terrain/plains.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"

namespace wg::terrain {

namespace detail = num::detail;

double band_weight(double z, const PlainsParams& p) noexcept {
  if (z >= p.z_lo && z <= p.z_hi) {
    return 1.0;
  }
  if (p.taper <= 0.0) {
    return 0.0;
  }
  const double outside = z < p.z_lo ? p.z_lo - z : z - p.z_hi;
  return std::max(0.0, 1.0 - (outside / p.taper));
}

core::Status plains_smooth(std::span<float> z, const core::Grid& grid, const PlainsParams& params,
                           core::Executor& exec, const core::CancelFlag* cancel,
                           core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "plains_smooth: z must have one value per cell")});
      !s.ok()) {
    return s;
  }
  const PlainsParams& p = params;
  if (!std::isfinite(p.z_lo) || !std::isfinite(p.z_hi) || !(p.z_lo <= p.z_hi) ||
      !std::isfinite(p.taper) || p.taper < 0.0 || !(p.lambda >= 0.0 && p.lambda <= 1.0) ||
      p.iterations < 0) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "plains_smooth: need z_lo <= z_hi, taper >= 0, lambda in [0, 1] "
                             "and iterations >= 0");
  }
  if (p.iterations == 0) {
    return {};
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<float>(z.size()), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<float>> scratch = arena->allocate<float>(z.size());
  if (!scratch) {
    return std::move(scratch.error());
  }

  std::span<float> src = z;
  std::span<float> dst = *scratch;
  const std::int32_t n = grid.n;
  for (std::int32_t it = 0; it < p.iterations; ++it) {
    const auto rows = [&](const core::ChunkRange& r) {
      for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const float v = src[k];
        const double w = band_weight(static_cast<double>(v), p);
        if (w == 0.0) {
          dst[k] = v;
          continue;
        }
        double sum = 0.0;
        for (const core::Dir8 d : core::kDir8) {
          const std::int64_t j = grid.neighbor(i, d);
          sum += static_cast<double>(j == core::kNoCell ? v : src[static_cast<std::size_t>(j)]);
        }
        const auto c = static_cast<double>(v);
        dst[k] = static_cast<float>(c + (p.lambda * w * ((sum / 8.0) - c)));
      }
    };
    if (core::Status s = core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel); !s.ok()) {
      return s;
    }
    std::swap(src, dst);
  }
  if (src.data() != z.data()) {
    std::copy(src.begin(), src.end(), z.begin());
  }
  return {};
}

}  // namespace wg::terrain
