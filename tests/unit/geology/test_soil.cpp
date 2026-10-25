#include "wg/geology/soil.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/executor.hpp"
#include "wg/geology/regions.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::geology::compute_soil_depth;
using wg::geology::GeoEnvironment;
using wg::geology::steady_soil_depth;
using wg::params::GeologyParams;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

}  // namespace

TEST_CASE("geology.steady_soil_depth balances production against transport") {
  const GeologyParams p;  // P_0 2e-4 m/yr, h_0 0.5 m, kappa 0.01 m^2/yr, cap 10 m
  // kappa C = P_0 e^(-h / h_0) for h = 1 m.
  const double c = p.soil_production * std::exp(-1.0 / p.soil_decay_depth) / p.soil_diffusivity;
  CHECK(steady_soil_depth(c, p) == doctest::Approx(1.0).epsilon(1e-12));
  CHECK(steady_soil_depth(0.0, p) == p.soil_max_depth);
  CHECK(steady_soil_depth(-1.0, p) == p.soil_max_depth);
  CHECK(steady_soil_depth(1e-12, p) == p.soil_max_depth);
  CHECK(steady_soil_depth(1e3, p) == 0.0);  // transport beyond production removes all soil
  CHECK(steady_soil_depth(c * 10.0, p) < steady_soil_depth(c, p));
  GeologyParams none = p;
  none.soil_production = 0.0;
  CHECK(steady_soil_depth(c, none) == 0.0);
  GeologyParams still = p;
  still.soil_diffusivity = 0.0;
  CHECK(steady_soil_depth(c, still) == p.soil_max_depth);
}

TEST_CASE("geology.compute_soil_depth thins soil on hilltops and clears mountains") {
  const Grid grid = wg::test::make_grid(33, 100.0, 10.0);
  // A cone peaking at the centre: convex everywhere except at the edges.
  const std::vector<float> z = wg::test::cone(grid, 250.0, 2.0, 16, 16);
  std::vector<std::uint8_t> e(u(grid.cells()), static_cast<std::uint8_t>(GeoEnvironment::land));
  e[u(grid.idx(0, 0))] = static_cast<std::uint8_t>(GeoEnvironment::mountain);
  std::vector<float> h(u(grid.cells()), -1.0F);
  wg::core::SerialExecutor exec;
  const GeologyParams p;
  REQUIRE(compute_soil_depth(z, e, grid, p, h, exec).ok());
  CHECK(h[u(grid.idx(0, 0))] == 0.0F);
  const float top = h[u(grid.idx(16, 16))];
  CHECK(top >= 0.0F);
  CHECK(top < static_cast<float>(p.soil_max_depth));
  for (const float v : h) {
    CHECK(v >= 0.0F);
    CHECK(v <= static_cast<float>(p.soil_max_depth));
  }
  // A flat field has no transport: every cell reaches the cap.
  const std::vector<float> flat(u(grid.cells()), 150.0F);
  REQUIRE(compute_soil_depth(flat, e, grid, p, h, exec).ok());
  CHECK(h[u(grid.idx(5, 5))] == static_cast<float>(p.soil_max_depth));
  GeologyParams bad = p;
  bad.soil_decay_depth = 0.0;
  CHECK(compute_soil_depth(z, e, grid, bad, h, exec).code == StatusCode::invalid_argument);
  std::vector<float> short_h(10);
  CHECK(compute_soil_depth(z, e, grid, p, short_h, exec).code == StatusCode::invalid_argument);
}
