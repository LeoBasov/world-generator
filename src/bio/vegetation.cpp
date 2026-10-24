#include "wg/bio/vegetation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/climate/temperature.hpp"
#include "wg/core/detmath.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/hydro/water.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::bio {

namespace detail = num::detail;

double miami_npp_temperature(double celsius) noexcept {
  return kNppMax / (1.0 + core::det_exp(1.315 - (0.119 * celsius)));
}

double miami_npp_precipitation(double rain_mm) noexcept {
  return kNppMax * (1.0 - core::det_exp(-0.000664 * rain_mm));
}

double miami_npp(double celsius, double rain_mm) noexcept {
  return std::min(miami_npp_temperature(celsius), miami_npp_precipitation(rain_mm));
}

core::Status compute_vegetation(const core::Grid& grid, const VegetationInputs& in,
                                const params::VegetationParams& p,
                                const params::TemperatureParams& temperature, std::span<float> out,
                                core::Executor& exec, const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.temperature, grid,
                           "compute_vegetation: temperature must have one value per cell"),
       detail::check_field(in.rainfall, grid,
                           "compute_vegetation: rainfall must have one value per cell"),
       detail::check_field(in.elevation, grid,
                           "compute_vegetation: elevation must have one value per cell"),
       detail::check_field(in.drainage, grid,
                           "compute_vegetation: drainage must have one value per cell"),
       detail::check_optional_field(in.water, grid,
                                    "compute_vegetation: water must have one value per cell"),
       detail::check_field(out, grid, "compute_vegetation: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const core::FieldRange& range = core::field_info(core::FieldId::vegetation).range;
  const auto sea = static_cast<double>(terrain::kSeaLevel);
  const auto lake = static_cast<std::uint8_t>(hydro::WaterClass::lake);
  const std::int64_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      const auto z = static_cast<double>(in.elevation[k]);
      const auto t = static_cast<double>(in.temperature[k]);
      const bool bare = z < sea || z > p.tree_line ||
                        is_ice(t, static_cast<double>(in.drainage[k])) ||
                        (!in.water.empty() && in.water[k] == lake);
      if (bare) {
        out[k] = 0.0F;
        continue;
      }
      const double npp = miami_npp(climate::game_to_celsius(t, temperature),
                                   p.rain_mm_per_index * static_cast<double>(in.rainfall[k]));
      out[k] = static_cast<float>(std::clamp(npp / kNppMax, range.min, range.max));
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel);
}

}  // namespace wg::bio
