#include "wg/terrain/band_repair.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"

using wg::terrain::BandTarget;

namespace {

BandTarget elevation_target(std::array<std::int32_t, 3> mins) {
  return {.lo = 0.0, .low_max = 400.0 / 3.0, .high_min = 800.0 / 3.0, .hi = 400.0, .mins = mins};
}

std::array<std::int64_t, 3> bands(const std::vector<float>& z) {
  std::array<std::int64_t, 3> n{};
  for (const float v : z) {
    const auto x = static_cast<double>(v);
    ++n[x < 400.0 / 3.0 ? 0 : (x < 800.0 / 3.0 ? 1 : 2)];
  }
  return n;
}

// m values rising evenly from lo to hi.
std::vector<float> ramp(std::size_t m, double lo, double hi) {
  std::vector<float> z(m);
  for (std::size_t i = 0; i < m; ++i) {
    z[i] =
        static_cast<float>(lo + ((hi - lo) * static_cast<double>(i) / static_cast<double>(m - 1)));
  }
  return z;
}

// True if the order of every pair is kept (weakly) and equal values stay equal.
bool order_kept(const std::vector<float>& before, const std::vector<float>& after) {
  for (std::size_t i = 0; i < before.size(); ++i) {
    for (std::size_t j = 0; j < before.size(); ++j) {
      if ((before[i] < before[j] && after[i] > after[j]) ||
          (before[i] == before[j] && after[i] != after[j])) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

TEST_CASE("terrain.band_repair leaves passing values untouched") {
  std::vector<float> z = ramp(300, 0.0, 400.0);
  const std::vector<float> before = z;
  auto r = wg::terrain::fit_elevation_bands(z, elevation_target({100, 100, 100}));
  REQUIRE(r);
  CHECK(*r);
  CHECK(z == before);
}

TEST_CASE("terrain.band_repair moves the cuts to the minimums, keeping order and the top") {
  // 300 values in [110, 210]: everything mid but the lowest 70.
  std::vector<float> z = ramp(300, 110.0, 210.0);
  z[17] = 400.0F;  // a summit
  const std::vector<float> before = z;
  const std::array<std::int64_t, 3> start = bands(z);
  REQUIRE(start[2] == 1);

  auto r = wg::terrain::fit_elevation_bands(z, elevation_target({120, 50, 40}));
  REQUIRE(r);
  CHECK(*r);
  // The low band grows to exactly its minimum, the high band starts at m - 40.
  CHECK(bands(z) == std::array<std::int64_t, 3>{120, 140, 40});
  CHECK(order_kept(before, z));
  CHECK(z[17] == 400.0F);
  CHECK(*std::min_element(z.begin(), z.end()) > 0.0F);
}

TEST_CASE("terrain.band_repair keeps an unchanged band's values") {
  // Low already has 100 squares; only high is short.
  std::vector<float> z = ramp(300, 0.0, 250.0);
  const std::vector<float> before = z;
  auto r = wg::terrain::fit_elevation_bands(z, elevation_target({100, 0, 30}));
  REQUIRE(r);
  CHECK(*r);
  const std::array<std::int64_t, 3> n = bands(z);
  CHECK(n[2] == 30);
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (static_cast<double>(before[i]) < 400.0 / 3.0) {
      CHECK(z[i] == before[i]);
    }
  }
}

TEST_CASE("terrain.band_repair shrinks a surplus band when mid is short") {
  // Two equal halves: 0 ... 100 (low) and 300 ... 400 (high), nothing mid.
  std::vector<float> z = ramp(200, 0.0, 100.0);
  const std::vector<float> top = ramp(200, 300.0, 400.0);
  z.insert(z.end(), top.begin(), top.end());
  const std::vector<float> before = z;
  auto r = wg::terrain::fit_elevation_bands(z, elevation_target({150, 120, 100}));
  REQUIRE(r);
  CHECK(*r);
  // Low gives up 50 squares first, then high gives up the remaining 70.
  CHECK(bands(z) == std::array<std::int64_t, 3>{150, 120, 130});
  CHECK(order_kept(before, z));
  CHECK(z.back() == 400.0F);
}

TEST_CASE("terrain.band_repair cannot split equal values") {
  // 250 cells on one plateau and 50 below: 100 low squares would split the plateau.
  std::vector<float> z(300, 200.0F);
  for (std::size_t i = 0; i < 50; ++i) {
    z[i] = 50.0F;
  }
  std::vector<float> plateau = z;
  auto r = wg::terrain::fit_elevation_bands(plateau, elevation_target({100, 1, 0}));
  REQUIRE(r);
  CHECK_FALSE(*r);
  CHECK(plateau == z);
  // The plateau may move as a whole: low takes all 300, if nothing else is needed.
  auto whole = wg::terrain::fit_elevation_bands(plateau, elevation_target({100, 0, 0}));
  REQUIRE(whole);
  CHECK(*whole);
  CHECK(bands(plateau) == std::array<std::int64_t, 3>{300, 0, 0});
  // More squares than cells can never be met.
  std::vector<float> small = ramp(10, 0.0, 400.0);
  auto too_many = wg::terrain::fit_elevation_bands(small, elevation_target({5, 5, 1}));
  REQUIRE(too_many);
  CHECK_FALSE(*too_many);
}

TEST_CASE("terrain.band_repair repairs a fractal field") {
  const wg::core::Grid grid = wg::test::make_grid(65);
  // White noise in [100, 300).
  std::vector<float> z = wg::test::white_noise(grid, 7);
  for (float& v : z) {
    v = (v * 200.0F) + 200.0F;
  }
  const std::vector<float> before = z;
  const auto m = static_cast<std::int32_t>(z.size());
  auto r = wg::terrain::fit_elevation_bands(z, elevation_target({m / 2, m / 4, m / 8}));
  REQUIRE(r);
  CHECK(*r);
  const std::array<std::int64_t, 3> n = bands(z);
  CHECK(n[0] >= m / 2);
  CHECK(n[1] >= m / 4);
  CHECK(n[2] >= m / 8);
  // Rank order (a weak order, since float rounding may merge neighbours).
  std::vector<std::size_t> idx(z.size());
  for (std::size_t i = 0; i < idx.size(); ++i) {
    idx[i] = i;
  }
  std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
    return before[a] < before[b] || (before[a] == before[b] && a < b);
  });
  for (std::size_t k = 1; k < idx.size(); ++k) {
    CHECK(z[idx[k - 1]] <= z[idx[k]]);
  }
}

TEST_CASE("terrain.band_repair rejects invalid input") {
  std::vector<float> z = {1.0F, 2.0F};
  std::vector<float> empty;
  CHECK(wg::terrain::fit_elevation_bands(empty, elevation_target({0, 0, 0})).error().code ==
        wg::core::StatusCode::invalid_argument);
  BandTarget bad = elevation_target({0, 0, 0});
  bad.high_min = bad.low_max;
  CHECK(wg::terrain::fit_elevation_bands(z, bad).error().code ==
        wg::core::StatusCode::invalid_argument);
  z[1] = std::nanf("");
  CHECK(wg::terrain::fit_elevation_bands(z, elevation_target({1, 0, 0})).error().code ==
        wg::core::StatusCode::invalid_argument);
}
