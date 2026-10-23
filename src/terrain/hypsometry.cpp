#include "wg/terrain/hypsometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/quantile.hpp"

namespace wg::terrain {

namespace detail = num::detail;

core::Status validate(const Hypsometry& h) noexcept {
  const auto bad = [](std::string_view message) {
    return core::make_status(core::StatusCode::invalid_argument, message);
  };
  if (h.z.size() < 2 || h.z.size() != h.cdf.size()) {
    return bad("hypsometry: z and cdf need the same number of knots, at least two");
  }
  for (std::size_t k = 0; k < h.z.size(); ++k) {
    if (!std::isfinite(h.z[k]) || !std::isfinite(h.cdf[k])) {
      return bad("hypsometry: knots must be finite");
    }
    if (k > 0 && (!(h.z[k - 1] < h.z[k]) || h.cdf[k - 1] > h.cdf[k])) {
      return bad("hypsometry: z must rise strictly and cdf must not fall");
    }
  }
  if (h.cdf.front() != 0.0 || h.cdf.back() != 1.0) {
    return bad("hypsometry: cdf must run from 0 to 1");
  }
  return {};
}

double hypsometry_cdf(const Hypsometry& h, double z) noexcept {
  if (z <= h.z.front()) {
    return h.cdf.front();
  }
  for (std::size_t k = 1; k < h.z.size(); ++k) {
    if (z <= h.z[k]) {
      const double t = (z - h.z[k - 1]) / (h.z[k] - h.z[k - 1]);
      return h.cdf[k - 1] + (t * (h.cdf[k] - h.cdf[k - 1]));
    }
  }
  return h.cdf.back();
}

double hypsometry_quantile(const Hypsometry& h, double p) noexcept {
  if (p <= h.cdf.front()) {
    return h.z.front();
  }
  // The first knot with cdf >= p closes the segment; cdf[k - 1] < p, so the segment is not flat.
  for (std::size_t k = 1; k < h.z.size(); ++k) {
    if (p <= h.cdf[k]) {
      const double t = (p - h.cdf[k - 1]) / (h.cdf[k] - h.cdf[k - 1]);
      return h.z[k - 1] + (t * (h.z[k] - h.z[k - 1]));
    }
  }
  return h.z.back();
}

core::Status hypsometric_remap(std::span<float> z, const Hypsometry& h, core::Executor& exec,
                               const core::CancelFlag* cancel,
                               core::MemoryResource& resource) noexcept {
  if (core::Status s = validate(h); !s.ok()) {
    return s;
  }
  if (z.empty()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "hypsometric_remap: no elevation values");
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<float>(z.size()), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<float>> f = arena->allocate<float>(z.size());
  if (!f) {
    return std::move(f.error());
  }
  if (core::Status s = num::empirical_cdf(z, {}, *f, resource); !s.ok()) {
    return s;
  }
  const std::span<const float> p = *f;
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      const auto k = static_cast<std::size_t>(i);
      z[k] = static_cast<float>(hypsometry_quantile(h, static_cast<double>(p[k])));
    }
  };
  return core::parallel_for(exec, 0, static_cast<std::int64_t>(z.size()), detail::kValueGrain, body,
                            cancel);
}

}  // namespace wg::terrain
