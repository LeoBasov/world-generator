#include "wg/climate/precipitation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::climate {

namespace detail = num::detail;

core::Status orographic_precipitation(const core::Grid& grid, const PrecipitationInputs& in,
                                      const params::PrecipitationParams& p, std::span<float> out,
                                      std::span<float> moisture, core::Executor& exec,
                                      const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.base, grid,
                           "orographic_precipitation: base must have one value per cell"),
       detail::check_field(in.elevation, grid,
                           "orographic_precipitation: elevation must have one value per cell"),
       detail::check_optional_field(
           in.drainage, grid, "orographic_precipitation: drainage must have one value per cell"),
       detail::check_field(out, grid, "orographic_precipitation: out must have one value per cell"),
       detail::check_optional_field(
           moisture, grid, "orographic_precipitation: moisture must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const bool quirk = p.df_drainage_quirk && p.orographic_precipitation;
  if (quirk && in.drainage.empty()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "orographic_precipitation: df_drainage_quirk needs drainage");
  }
  const std::int64_t n = grid.n;
  if (!p.orographic_precipitation) {
    const auto copy = [&](const core::ChunkRange& r) {
      for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
        out[static_cast<std::size_t>(i)] = in.base[static_cast<std::size_t>(i)];
      }
    };
    return core::parallel_for(exec, 0, n, detail::kRowGrain, copy, cancel);
  }

  const core::FieldRange& range = core::field_info(core::FieldId::rainfall).range;
  const auto sea = static_cast<double>(terrain::kSeaLevel);
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t y = r.begin; y < r.end; ++y) {
      double q = p.q_max;
      double surface_prev = 0.0;
      for (std::int64_t x = 0; x < n; ++x) {
        const auto k = static_cast<std::size_t>((y * n) + x);
        const auto z = static_cast<double>(in.elevation[k]);
        const double surface = std::max(z, sea);
        const auto base = static_cast<double>(in.base[k]);
        double rain = 0.0;
        if (quirk && static_cast<double>(in.drainage[k]) < kQuirkDrainageMin) {
          rain = std::clamp(base, range.min, range.max);
          out[k] = in.base[k];
        } else {
          rain = base * (q / p.q_max);
          if (x > 0) {
            rain += p.orographic_gain * q * std::max(0.0, surface - surface_prev);
          }
          rain = std::clamp(rain, range.min, range.max);
          out[k] = static_cast<float>(rain);
        }
        const double recharge = z < sea ? p.recharge_ocean : p.recharge_land;
        q = std::clamp(q - (p.draw_fraction * rain) + recharge, 0.0, p.q_max);
        if (!moisture.empty()) {
          moisture[k] = static_cast<float>(q);
        }
        surface_prev = surface;
      }
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel);
}

}  // namespace wg::climate
