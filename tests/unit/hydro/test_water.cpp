#include "wg/hydro/water.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "support/river_fixture.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::hydro::WaterClass;

namespace {

std::uint8_t w(WaterClass c) { return static_cast<std::uint8_t>(c); }

}  // namespace

TEST_CASE("hydro.classify_water: ocean, lake, river and land") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = wg::test::ramp(grid, 99.0, 2.0);  // column 0 ocean
  std::vector<std::int32_t> lake_id(z.size(), -1);
  const auto lake = static_cast<std::size_t>(grid.idx(12, 12));
  lake_id[lake] = 0;
  wg::core::RiverGraph rivers = wg::test::river_fixture(grid);
  rivers.width_class[3] = 4;
  std::vector<std::uint8_t> water(z.size(), 9);
  std::vector<std::uint8_t> river_class(z.size(), 9);
  REQUIRE(wg::hydro::classify_water(z, lake_id, rivers, grid, water, river_class).ok());
  // The fixture's mouth node lies on the ocean column and stays ocean.
  CHECK(water[static_cast<std::size_t>(grid.idx(0, 4))] == w(WaterClass::ocean));
  CHECK(river_class[static_cast<std::size_t>(grid.idx(0, 4))] == 0);
  CHECK(water[static_cast<std::size_t>(grid.idx(3, 4))] == w(WaterClass::river));
  CHECK(river_class[static_cast<std::size_t>(grid.idx(3, 4))] == 4);
  CHECK(river_class[static_cast<std::size_t>(grid.idx(5, 2))] == 1);
  CHECK(water[lake] == w(WaterClass::lake));
  CHECK(water[static_cast<std::size_t>(grid.idx(10, 10))] == w(WaterClass::land));
  CHECK(river_class[static_cast<std::size_t>(grid.idx(10, 10))] == 0);

  rivers.cell[2] = grid.cells();
  CHECK(wg::hydro::classify_water(z, lake_id, rivers, grid, water, river_class).code ==
        StatusCode::invalid_argument);
  rivers.order.pop_back();
  CHECK(wg::hydro::classify_water(z, lake_id, rivers, grid, water, river_class).code ==
        StatusCode::invalid_argument);
  std::vector<std::uint8_t> short_water(5);
  CHECK(wg::hydro::classify_water(z, lake_id, wg::test::river_fixture(grid), grid, short_water,
                                  river_class)
            .code == StatusCode::invalid_argument);
}
