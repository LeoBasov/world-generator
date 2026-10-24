#include "wg/hydro/water.hpp"

#include <algorithm>
#include <cstddef>

#include "numerics/common.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

core::Status classify_water(std::span<const float> z, std::span<const std::int32_t> lake_id,
                            const core::RiverGraph& rivers, const core::Grid& grid,
                            std::span<std::uint8_t> water,
                            std::span<std::uint8_t> river_class) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "classify_water: z must have one value per cell"),
           detail::check_field(lake_id, grid,
                               "classify_water: lake_id must have one value per cell"),
           detail::check_field(water, grid, "classify_water: water must have one value per cell"),
           detail::check_field(river_class, grid,
                               "classify_water: river_class must have one value per cell")});
      !s.ok()) {
    return s;
  }
  if (!rivers.consistent()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "classify_water: the river graph arrays differ in length");
  }
  for (const std::int64_t c : rivers.cell) {
    if (!grid.contains(c)) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "classify_water: a river cell is out of range");
    }
  }
  std::fill(river_class.begin(), river_class.end(), std::uint8_t{0});
  for (std::size_t i = 0; i < z.size(); ++i) {
    WaterClass w = WaterClass::land;
    if (z[i] < terrain::kSeaLevel) {
      w = WaterClass::ocean;
    } else if (lake_id[i] >= 0) {
      w = WaterClass::lake;
    }
    water[i] = static_cast<std::uint8_t>(w);
  }
  for (std::size_t k = 0; k < rivers.size(); ++k) {
    const auto c = static_cast<std::size_t>(rivers.cell[k]);
    if (water[c] == static_cast<std::uint8_t>(WaterClass::land)) {
      water[c] = static_cast<std::uint8_t>(WaterClass::river);
      river_class[c] = rivers.width_class[k];
    }
  }
  return {};
}

}  // namespace wg::hydro
