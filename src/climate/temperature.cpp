#include "wg/climate/temperature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/climate/latitude.hpp"
#include "wg/generated/field_ids.hpp"

namespace wg::climate {

namespace detail = num::detail;

core::Status compute_temperature(const core::Grid& grid, const TemperatureInputs& in,
                                 const params::TemperatureParams& p, std::span<float> out,
                                 core::Executor& exec, const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.base, grid, "compute_temperature: base must have one value per cell"),
       detail::check_optional_field(in.latitude, grid,
                                    "compute_temperature: latitude must have one value per cell"),
       detail::check_optional_field(in.elevation, grid,
                                    "compute_temperature: elevation must have one value per cell"),
       detail::check_optional_field(in.vegetation, grid,
                                    "compute_temperature: vegetation must have one value per cell"),
       detail::check_optional_field(in.rainfall, grid,
                                    "compute_temperature: rainfall must have one value per cell"),
       detail::check_field(out, grid, "compute_temperature: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const core::FieldRange& range = core::field_info(core::FieldId::temperature).range;
  const double hu_m = grid.hu_m;
  const std::int64_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      auto t = static_cast<double>(in.base[k]);
      if (!in.latitude.empty()) {
        t += latitude_term(static_cast<double>(in.latitude[k]), p.equator_pole_delta);
      }
      if (!in.elevation.empty()) {
        t +=
            lapse_term(static_cast<double>(in.elevation[k]), p.lapse_rate, hu_m, p.game_per_kelvin);
      }
      if (!in.vegetation.empty()) {
        t += vegetation_term(static_cast<double>(in.vegetation[k]), p.forest_damping);
      }
      if (!in.rainfall.empty()) {
        t += rain_term(static_cast<double>(in.rainfall[k]), p.rain_damping);
      }
      out[k] = static_cast<float>(std::clamp(t, range.min, range.max));
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel);
}

}  // namespace wg::climate
