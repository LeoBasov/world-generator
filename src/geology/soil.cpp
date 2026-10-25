#include "wg/geology/soil.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/core/detmath.hpp"
#include "wg/geology/regions.hpp"
#include "wg/numerics/laplacian.hpp"

namespace wg::geology {

namespace detail = num::detail;

double steady_soil_depth(double convexity_per_m, const params::GeologyParams& params) noexcept {
  const double p0 = params.soil_production;
  const double h0 = params.soil_decay_depth;
  const double h_max = params.soil_max_depth;
  if (!(p0 > 0.0) || !(h_max > 0.0)) {
    return 0.0;
  }
  // Transport below P_0 e^(-h_max / h_0) would need a depth beyond the cap.
  const double transport = params.soil_diffusivity * convexity_per_m;
  if (!(transport > p0 * core::det_exp(-h_max / h0))) {
    return h_max;
  }
  return std::clamp(h0 * core::det_log(p0 / transport), 0.0, h_max);
}

core::Status compute_soil_depth(std::span<const float> elevation,
                                std::span<const std::uint8_t> environment, const core::Grid& grid,
                                const params::GeologyParams& params, std::span<float> soil_depth,
                                core::Executor& exec, const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(elevation, grid,
                           "compute_soil_depth: elevation must have one value per cell"),
       detail::check_field(environment, grid,
                           "compute_soil_depth: environment must have one value per cell"),
       detail::check_field(soil_depth, grid,
                           "compute_soil_depth: soil_depth must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  if (!(params.soil_production >= 0.0) || !(params.soil_decay_depth > 0.0) ||
      !(params.soil_diffusivity >= 0.0) || !(params.soil_max_depth >= 0.0)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "compute_soil_depth: soil parameters must not be negative");
  }
  // The Laplacian [eu / cell^2] goes into the output first.
  if (core::Status s = num::laplacian(elevation, {}, grid, soil_depth, exec); !s.ok()) {
    return s;
  }
  const double to_per_m = grid.hu_m / (grid.dx_m * grid.dx_m);
  return core::parallel_for(
      exec, 0, grid.cells(), detail::kRowGrain * grid.n,
      [&](const core::ChunkRange& r) noexcept {
        for (std::int64_t i = r.begin; i < r.end; ++i) {
          const auto c = static_cast<std::size_t>(i);
          if (environment[c] == static_cast<std::uint8_t>(GeoEnvironment::mountain)) {
            soil_depth[c] = 0.0F;
            continue;
          }
          const double convexity = -static_cast<double>(soil_depth[c]) * to_per_m;
          soil_depth[c] = static_cast<float>(steady_soil_depth(convexity, params));
        }
      },
      cancel);
}

}  // namespace wg::geology
