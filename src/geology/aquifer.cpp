#include "wg/geology/aquifer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::geology {

namespace detail = num::detail;

bool column_has_aquifer(const core::GeoColumn& column, const GeologyTables& tables) noexcept {
  return std::any_of(column.soil_layers.begin(), column.soil_layers.end(),
                     [&](const core::SoilLayer& l) {
                       return l.soil_id < tables.soil_count() && tables.soil_aquifer(l.soil_id);
                     }) ||
         std::any_of(column.layers.begin(), column.layers.end(), [&](const core::RockLayer& l) {
           return l.rock_id < tables.rock_count() && tables.rock_aquifer(l.rock_id);
         });
}

double aquifer_probability(float elevation, float drainage) noexcept {
  if (!std::isfinite(elevation) || !std::isfinite(drainage)) {
    return 0.0;
  }
  const double d = std::clamp(static_cast<double>(drainage), 0.0, 100.0);
  const double height = std::clamp(
      (static_cast<double>(elevation) - static_cast<double>(terrain::kSeaLevel)) / 200.0, 0.0, 1.0);
  return (1.0 - (d / 100.0)) * (1.0 - height);
}

core::Status assign_aquifers(const AquiferInputs& in, const core::Grid& grid, bool heavy_quirk,
                             core::RngKey key, std::span<std::uint8_t> aquifer,
                             core::Executor& exec, const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.elevation, grid,
                           "assign_aquifers: elevation must have one value per cell"),
       detail::check_field(in.drainage, grid,
                           "assign_aquifers: drainage must have one value per cell"),
       detail::check_field(in.environment, grid,
                           "assign_aquifers: environment must have one value per cell"),
       detail::check_field(in.geo_region, grid,
                           "assign_aquifers: geo_region must have one value per cell"),
       detail::check_field(aquifer, grid,
                           "assign_aquifers: aquifer must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const auto regions = static_cast<std::int64_t>(in.region_has_aquifer.size());
  if (std::any_of(in.geo_region.begin(), in.geo_region.end(),
                  [&](std::int32_t r) { return r < 0 || r >= regions; })) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "assign_aquifers: geo_region out of range");
  }
  const core::RngKey k = key.derive("aquifer");
  return core::parallel_for(
      exec, 0, grid.cells(), detail::kRowGrain * grid.n,
      [&](const core::ChunkRange& r) noexcept {
        for (std::int64_t i = r.begin; i < r.end; ++i) {
          const auto c = static_cast<std::size_t>(i);
          const auto e = static_cast<GeoEnvironment>(in.environment[c]);
          AquiferClass a = AquiferClass::none;
          if (!is_ocean(e) && e != GeoEnvironment::mountain &&
              in.region_has_aquifer[static_cast<std::size_t>(in.geo_region[c])] != 0 &&
              core::uniform01(k, static_cast<std::uint64_t>(i), 0) <
                  aquifer_probability(in.elevation[c], in.drainage[c])) {
            a = AquiferClass::light;
            const double d = std::clamp(static_cast<double>(in.drainage[c]), 0.0, 100.0);
            const bool heavy = heavy_quirk ? static_cast<std::int32_t>(std::floor(d)) % 20 == 7
                                           : core::uniform01(k, static_cast<std::uint64_t>(i), 1) <
                                                 kHeavyAquiferChance;
            if (heavy) {
              a = AquiferClass::heavy;
            }
          }
          aquifer[c] = static_cast<std::uint8_t>(a);
        }
      },
      cancel);
}

}  // namespace wg::geology
