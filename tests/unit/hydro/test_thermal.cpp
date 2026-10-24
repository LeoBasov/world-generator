#include "wg/hydro/thermal.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/hydro/test_rivers.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::hydro::ThermalParams;

namespace {

double sum(const std::vector<float>& z) {
  double s = 0.0;
  for (const float v : z) {
    s += static_cast<double>(v);
  }
  return s;
}

// Largest difference between 8-neighbours.
double max_difference(const std::vector<float>& z, const Grid& grid) {
  double largest = 0.0;
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    for (const wg::core::Dir8 d : wg::core::kDir8) {
      const std::int64_t j = grid.neighbor(i, d);
      if (j != wg::core::kNoCell) {
        largest = std::max(largest, std::abs(static_cast<double>(z[static_cast<std::size_t>(i)]) -
                                             static_cast<double>(z[static_cast<std::size_t>(j)])));
      }
    }
  }
  return largest;
}

// Random relief 100 + 300 u, u in [-0.5, 0.5), on the lattice.
std::vector<float> rough(const Grid& grid, std::uint64_t seed) {
  std::vector<float> z = wg::test::white_noise(grid, seed, "test.hydro.thermal");
  for (float& v : z) {
    v = 100.0F + (300.0F * v);
  }
  REQUIRE(wg::hydro::snap_to_lattice(z).ok());
  return z;
}

}  // namespace

TEST_CASE("hydro.thermal_erode conserves the sum and ends within Theta (A16 acceptance)") {
  const Grid grid = wg::test::make_grid(33);
  std::vector<std::vector<float>> fields = {rough(grid, 1), rough(grid, 2),
                                            wg::test::cone(grid, 400.0, 40.0, 16, 16)};
  REQUIRE(wg::hydro::snap_to_lattice(fields.back()).ok());
  for (const double theta : {30.0, 7.25}) {
    for (const double c : {0.5, 1.0, 0.1}) {
      for (const std::vector<float>& field : fields) {
        CAPTURE(theta);
        CAPTURE(c);
        std::vector<float> z = field;
        REQUIRE(max_difference(z, grid) > theta);
        wg::core::Expected<std::int64_t> transfers = wg::hydro::thermal_erode(
            z, grid, ThermalParams{.talus_threshold = theta, .fraction = c});
        REQUIRE(transfers.has_value());
        CHECK(*transfers > 0);
        CHECK(sum(z) == sum(field));
        CHECK(max_difference(z, grid) <= theta);
        // Material only moves towards the mean.
        CHECK(*std::max_element(z.begin(), z.end()) <=
              *std::max_element(field.begin(), field.end()));
        CHECK(*std::min_element(z.begin(), z.end()) >=
              *std::min_element(field.begin(), field.end()));
        for (const float v : z) {
          CHECK(wg::hydro::on_lattice(static_cast<double>(v)));
        }
      }
    }
  }
}

TEST_CASE("hydro.thermal_erode leaves gentle slopes untouched") {
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = wg::test::ramp(grid, 100.0, 10.0);
  const std::vector<float> before = z;
  wg::core::Expected<std::int64_t> transfers = wg::hydro::thermal_erode(z, grid, ThermalParams{});
  REQUIRE(transfers.has_value());
  CHECK(*transfers == 0);
  CHECK(z == before);

  // A single cliff only changes the cells next to it.
  std::vector<float> cliff = wg::test::ramp(grid, 100.0, 1.0);
  cliff[static_cast<std::size_t>(grid.idx(16, 16))] = 300.0F;
  const std::vector<float> cliff_before = cliff;
  REQUIRE(wg::hydro::thermal_erode(cliff, grid, ThermalParams{}).has_value());
  CHECK(sum(cliff) == sum(cliff_before));
  CHECK(max_difference(cliff, grid) <= 30.0);
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const std::int32_t ddx = grid.x_of(i) - 16;
    const std::int32_t ddy = grid.y_of(i) - 16;
    if (std::max(std::abs(ddx), std::abs(ddy)) > 8) {
      CHECK(cliff[static_cast<std::size_t>(i)] == cliff_before[static_cast<std::size_t>(i)]);
    }
  }
}

TEST_CASE("hydro.thermal_erode with Theta below one quantum flattens to one quantum") {
  const Grid grid = wg::test::make_grid(9);
  std::vector<float> z = rough(grid, 4);
  const std::vector<float> before = z;
  REQUIRE(wg::hydro::thermal_erode(z, grid, ThermalParams{.talus_threshold = 0.0, .fraction = 1.0})
              .has_value());
  CHECK(sum(z) == sum(before));
  CHECK(max_difference(z, grid) <= wg::hydro::kErosionQuantum);
}

TEST_CASE("hydro.thermal_erode with fraction 0 changes nothing") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = rough(grid, 5);
  const std::vector<float> before = z;
  wg::core::Expected<std::int64_t> transfers =
      wg::hydro::thermal_erode(z, grid, ThermalParams{.fraction = 0.0});
  REQUIRE(transfers.has_value());
  CHECK(*transfers == 0);
  CHECK(z == before);
}

TEST_CASE("hydro.thermal_erode rejects invalid arguments") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = rough(grid, 6);
  CHECK(wg::hydro::thermal_erode(z, Grid{}, ThermalParams{}).error().code ==
        StatusCode::invalid_argument);
  std::vector<float> short_z(4);
  CHECK(wg::hydro::thermal_erode(short_z, grid, ThermalParams{}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::thermal_erode(z, grid, ThermalParams{.talus_threshold = -1.0}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::thermal_erode(
            z, grid, ThermalParams{.talus_threshold = std::numeric_limits<double>::infinity()})
            .error()
            .code == StatusCode::invalid_argument);
  CHECK(wg::hydro::thermal_erode(z, grid, ThermalParams{.fraction = 1.5}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::thermal_erode(
            z, grid, ThermalParams{.fraction = std::numeric_limits<double>::quiet_NaN()})
            .error()
            .code == StatusCode::invalid_argument);
  CHECK(wg::hydro::thermal_erode(z, grid, ThermalParams{.max_transfers = 0}).error().code ==
        StatusCode::invalid_argument);

  // Values off the lattice are rejected and left as they are.
  std::vector<float> off = z;
  off[3] = 0.1F;
  CHECK(wg::hydro::thermal_erode(off, grid, ThermalParams{}).error().code ==
        StatusCode::invalid_argument);
  CHECK(off[3] == 0.1F);

  // Too few transfers to converge is an internal error.
  CHECK(wg::hydro::thermal_erode(z, grid, ThermalParams{.max_transfers = 1}).error().code ==
        StatusCode::internal);

  wg::test::CountingResource resource;
  resource.fail = true;
  std::vector<float> fresh = rough(grid, 6);
  CHECK(wg::hydro::thermal_erode(fresh, grid, ThermalParams{}, resource).error().code ==
        StatusCode::out_of_memory);
  CHECK(fresh == z);
}
