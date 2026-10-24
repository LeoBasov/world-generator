#include "wg/hydro/priority_flood.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/numerics/neighborhood.hpp"

using wg::core::Grid;
using wg::core::StatusCode;

namespace {

// Random relief 100 + 200 u with u in [-0.5, 0.5): about half ocean, many pits and basins.
std::vector<float> random_relief(const Grid& grid, std::uint64_t seed) {
  std::vector<float> z = wg::test::white_noise(grid, seed, "test.hydro.relief");
  for (float& v : z) {
    v = 100.0F + (200.0F * v);
  }
  return z;
}

// Reference water level by relaxation: seeds keep z, every other cell starts at +infinity and
// is lowered to max(z_i, min over the neighbours) until nothing changes.
std::vector<float> reference_fill(const std::vector<float>& z, const Grid& grid) {
  const float inf = std::numeric_limits<float>::infinity();
  std::vector<float> w(z.size(), inf);
  bool any_ocean = false;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (z[i] < 100.0F) {
      w[i] = z[i];
      any_ocean = true;
    }
  }
  if (!any_ocean) {
    const auto lowest = static_cast<std::size_t>(std::min_element(z.begin(), z.end()) - z.begin());
    w[lowest] = z[lowest];
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::int64_t i = 0; i < grid.cells(); ++i) {
      const auto iu = static_cast<std::size_t>(i);
      float lowest = inf;
      wg::num::for_each_neighbor(grid, i, wg::num::Connectivity::d8,
                                 [&](std::int64_t j, wg::core::Dir8 /*d*/) {
                                   lowest = std::min(lowest, w[static_cast<std::size_t>(j)]);
                                 });
      const float v = std::max(z[iu], lowest);
      if (v < w[iu]) {
        w[iu] = v;
        changed = true;
      }
    }
  }
  return w;
}

}  // namespace

TEST_CASE("hydro.fill_depressions equals the relaxation reference on random relief") {
  const Grid grid = wg::test::make_grid(33);
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    CAPTURE(seed);
    const std::vector<float> z = random_relief(grid, seed);
    std::vector<float> w(z.size());
    REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
    CHECK(w == reference_fill(z, grid));
  }
}

TEST_CASE("hydro.fill_depressions fills the bowl to its notch") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = wg::test::bowl(grid);
  std::vector<float> w(z.size());
  REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
  CHECK(w == reference_fill(z, grid));
  // Inside the rim (r < 10) the water stands at the notch height 190; elsewhere w = z.
  const std::int32_t c = (grid.n - 1) / 2;
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      const auto i = static_cast<std::size_t>(grid.idx(x, y));
      const bool inside = ((x - c) * (x - c)) + ((y - c) * (y - c)) < 100;
      CHECK(w[i] == (inside ? 190.0F : z[i]));
    }
  }
}

TEST_CASE("hydro.fill_depressions without ocean drains to the lowest cell") {
  const Grid grid = wg::test::make_grid(17);
  // A cone pointing down (a funnel) with its tip at (3, 5), all above sea level, and a pit at
  // (12, 12) that must fill up to its lowest neighbour.
  std::vector<float> z = wg::test::cone(grid, 120.0, -1.0, 3, 5);
  z[static_cast<std::size_t>(grid.idx(12, 12))] = 125.0F;
  std::vector<float> w(z.size());
  REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
  CHECK(w == reference_fill(z, grid));
  CHECK(w[static_cast<std::size_t>(grid.idx(3, 5))] == 120.0F);
  CHECK(w[static_cast<std::size_t>(grid.idx(12, 12))] > 125.0F);
}

TEST_CASE("hydro.priority_flood lies on or above the water level and rises strictly on flats") {
  const Grid grid = wg::test::make_grid(33);
  for (const std::uint64_t seed : {1U, 2U}) {
    CAPTURE(seed);
    const std::vector<float> z = random_relief(grid, seed);
    std::vector<float> w(z.size());
    std::vector<float> zf(z.size());
    REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
    REQUIRE(wg::hydro::priority_flood(z, grid, zf).ok());
    for (std::size_t i = 0; i < z.size(); ++i) {
      CHECK(zf[i] >= w[i]);
      if (z[i] < 100.0F) {
        CHECK(zf[i] == z[i]);
      }
    }
  }
  // A ramp has neither pits nor flats: the surface is unchanged.
  const std::vector<float> ramp = wg::test::ramp(grid, 99.0, 1.0);
  std::vector<float> zf(ramp.size());
  REQUIRE(wg::hydro::priority_flood(ramp, grid, zf).ok());
  CHECK(zf == ramp);
}

TEST_CASE("hydro.priority_flood raises a plateau ring by ring (epsilon routing on flats)") {
  const Grid grid = wg::test::make_grid(17);
  constexpr std::int32_t kMargin = 4;
  const std::vector<float> z = wg::test::plateau(grid, kMargin);
  std::vector<float> zf(z.size());
  REQUIRE(wg::hydro::priority_flood(z, grid, zf, {.epsilon = 0.5F}).ok());
  // Ring k of the flat top (k cells inside its rim) stands at 300 + k * 0.5 exactly.
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      const std::int32_t edge = std::min(std::min(x, y), std::min(grid.n - 1 - x, grid.n - 1 - y));
      const float v = zf[static_cast<std::size_t>(grid.idx(x, y))];
      if (edge >= kMargin) {
        CHECK(v == 300.0F + (0.5F * static_cast<float>(edge - kMargin)));
      } else {
        CHECK(v == z[static_cast<std::size_t>(grid.idx(x, y))]);
      }
    }
  }
}

TEST_CASE("hydro.priority_flood rounds a tiny epsilon up to the next float") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = wg::test::plateau(grid, 4);
  std::vector<float> zf(z.size());
  REQUIRE(wg::hydro::priority_flood(z, grid, zf, {.epsilon = 1e-9F}).ok());
  const float ring1 = zf[static_cast<std::size_t>(grid.idx(5, 8))];
  CHECK(ring1 == std::nextafter(300.0F, 400.0F));
  CHECK(zf[static_cast<std::size_t>(grid.idx(6, 8))] == std::nextafter(ring1, 400.0F));
}

TEST_CASE("hydro.priority_flood and fill_depressions reject invalid input") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::ramp(grid, 99.0, 1.0);
  std::vector<float> out(z.size());
  std::vector<float> short_out(5);
  CHECK(wg::hydro::fill_depressions(z, grid, short_out).code == StatusCode::invalid_argument);
  CHECK(wg::hydro::priority_flood(z, grid, short_out).code == StatusCode::invalid_argument);
  CHECK(wg::hydro::fill_depressions(z, Grid{}, out).code == StatusCode::invalid_argument);
  for (const float eps : {0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
    CAPTURE(eps);
    CHECK(wg::hydro::priority_flood(z, grid, out, {.epsilon = eps}).code ==
          StatusCode::invalid_argument);
  }
  z[3] = std::numeric_limits<float>::quiet_NaN();
  CHECK(wg::hydro::fill_depressions(z, grid, out).code == StatusCode::invalid_argument);
  z[3] = 100.0F;
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::hydro::fill_depressions(z, grid, out, resource).code == StatusCode::out_of_memory);
  CHECK(wg::hydro::priority_flood(z, grid, out, {}, resource).code == StatusCode::out_of_memory);
}
