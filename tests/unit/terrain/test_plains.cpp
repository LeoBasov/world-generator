#include "wg/terrain/plains.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/fractal/fractal.hpp"
#include "wg/params/params.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::terrain::PlainsParams;

namespace {

// Clamped fractal elevation in [1, 400], as C5 sees it.
std::vector<float> elevation(const Grid& grid, std::uint64_t seed) {
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  wg::core::SerialExecutor exec;
  const wg::params::GenParams p;
  REQUIRE(wg::fractal::generate_field(grid, p.fields.elevation,
                                      wg::core::stage_key(seed, "test.plains", 0), {}, z, exec)
              .ok());
  return z;
}

// Mean squared difference between D4 neighbours inside [lo, hi] (roughness of the band).
double roughness(const std::vector<float>& z, const Grid& grid, double lo, double hi) {
  double sum = 0.0;
  std::int64_t count = 0;
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const double v = z[static_cast<std::size_t>(i)];
    const std::int64_t j = grid.neighbor(i, wg::core::Dir8::e);
    if (j == wg::core::kNoCell || v < lo || v > hi) {
      continue;
    }
    const double d = v - static_cast<double>(z[static_cast<std::size_t>(j)]);
    sum += d * d;
    ++count;
  }
  return sum / static_cast<double>(count);
}

}  // namespace

TEST_CASE("terrain.band_weight is 1 in the band and tapers linearly to 0") {
  const PlainsParams p{.z_lo = 110.0, .z_hi = 290.0, .taper = 10.0};
  CHECK(wg::terrain::band_weight(110.0, p) == 1.0);
  CHECK(wg::terrain::band_weight(200.0, p) == 1.0);
  CHECK(wg::terrain::band_weight(290.0, p) == 1.0);
  CHECK(wg::terrain::band_weight(105.0, p) == doctest::Approx(0.5));
  CHECK(wg::terrain::band_weight(297.5, p) == doctest::Approx(0.25));
  CHECK(wg::terrain::band_weight(100.0, p) == 0.0);
  CHECK(wg::terrain::band_weight(300.0, p) == 0.0);
  CHECK(wg::terrain::band_weight(50.0, p) == 0.0);
  const PlainsParams step{.z_lo = 110.0, .z_hi = 290.0, .taper = 0.0};
  CHECK(wg::terrain::band_weight(109.999, step) == 0.0);
  CHECK(wg::terrain::band_weight(110.0, step) == 1.0);
}

TEST_CASE("terrain.plains_smooth leaves cells outside the band bit-identical (A13 acceptance)") {
  const Grid grid = wg::test::make_grid(129);
  const PlainsParams p;
  for (const std::uint64_t seed : {1U, 2U}) {
    CAPTURE(seed);
    const std::vector<float> before = elevation(grid, seed);
    std::vector<float> z = before;
    wg::core::SerialExecutor exec;
    REQUIRE(wg::terrain::plains_smooth(z, grid, p, exec).ok());
    std::int64_t outside = 0;
    std::int64_t changed = 0;
    for (std::size_t k = 0; k < z.size(); ++k) {
      const double v = before[k];
      if (v <= p.z_lo - p.taper || v >= p.z_hi + p.taper) {
        ++outside;
        CHECK(std::memcmp(&z[k], &before[k], sizeof(float)) == 0);
      } else if (z[k] != before[k]) {
        ++changed;
      }
    }
    CHECK(outside > 0);
    CHECK(changed > 0);
    CHECK(roughness(z, grid, p.z_lo, p.z_hi) < roughness(before, grid, p.z_lo, p.z_hi));
  }
}

TEST_CASE("terrain.plains_smooth does nothing with lambda 0 or no iterations") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> before = elevation(grid, 3);
  wg::core::SerialExecutor exec;
  std::vector<float> z = before;
  REQUIRE(wg::terrain::plains_smooth(z, grid, PlainsParams{.lambda = 0.0}, exec).ok());
  CHECK(z == before);
  REQUIRE(wg::terrain::plains_smooth(z, grid, PlainsParams{.iterations = 0}, exec).ok());
  CHECK(z == before);
}

TEST_CASE("terrain.plains_smooth flattens a band cell towards its neighbours' mean") {
  const Grid grid = wg::test::make_grid(5);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 200.0F);
  const auto centre = static_cast<std::size_t>(grid.idx(2, 2));
  z[centre] = 240.0F;
  wg::core::SerialExecutor exec;
  const PlainsParams p{.lambda = 0.5, .iterations = 1};
  REQUIRE(wg::terrain::plains_smooth(z, grid, p, exec).ok());
  CHECK(z[centre] == 220.0F);
  // Each neighbour sees one raised cell of eight: 200 + 0.5 * 40 / 8.
  CHECK(z[static_cast<std::size_t>(grid.idx(1, 1))] == 202.5F);
  // A corner sees off-grid neighbours as itself (zero flux) and no raised cell.
  CHECK(z[0] == 200.0F);
}

TEST_CASE("terrain.plains_smooth is independent of the thread count") {
  const Grid grid = wg::test::make_grid(129);
  std::vector<float> a = elevation(grid, 4);
  std::vector<float> b = a;
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  REQUIRE(wg::terrain::plains_smooth(a, grid, PlainsParams{}, serial).ok());
  REQUIRE(wg::terrain::plains_smooth(b, grid, PlainsParams{}, **pool).ok());
  CHECK(a == b);
}

TEST_CASE("terrain.plains_smooth refuses bad input and observes cancel") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 150.0F);
  wg::core::SerialExecutor exec;
  CHECK(
      wg::terrain::plains_smooth(z, grid, PlainsParams{.z_lo = 300.0, .z_hi = 200.0}, exec).code ==
      StatusCode::invalid_argument);
  CHECK(wg::terrain::plains_smooth(z, grid, PlainsParams{.taper = -1.0}, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::plains_smooth(z, grid, PlainsParams{.lambda = 1.5}, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::plains_smooth(z, grid, PlainsParams{.iterations = -1}, exec).code ==
        StatusCode::invalid_argument);
  std::vector<float> short_z(4, 150.0F);
  CHECK(wg::terrain::plains_smooth(short_z, grid, PlainsParams{}, exec).code ==
        StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::terrain::plains_smooth(z, grid, PlainsParams{}, exec, &cancel).code ==
        StatusCode::cancelled);
}
