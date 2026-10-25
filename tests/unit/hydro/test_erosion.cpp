#include "wg/hydro/erosion.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/hydro/priority_flood.hpp"
#include "wg/hydro/test_rivers.hpp"
#include "wg/hydro/thermal.hpp"

using wg::core::Grid;
using wg::core::RngKey;
using wg::core::StatusCode;
using wg::hydro::ErosionReport;
using wg::params::ErosionParams;

namespace {

const RngKey kKey = wg::core::stage_key(42, "test.hydro.erosion", 0);

ErosionParams cycles(std::int32_t count, bool thermal) {
  ErosionParams p;
  p.erosion_cycle_count = count;
  p.periodically_erode_extremes = thermal;
  return p;
}

ErosionReport run(std::vector<float>& z, const Grid& grid, const ErosionParams& params) {
  wg::core::Expected<ErosionReport> report = wg::hydro::erode(z, grid, params, kKey);
  REQUIRE(report.has_value());
  return *report;
}

// Ponded volume sum(w - z) of the plain fill, and the water depth at cell i.
double ponded(const std::vector<float>& z, const Grid& grid) {
  std::vector<float> w(z.size());
  REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
  double volume = 0.0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    volume += static_cast<double>(w[i]) - static_cast<double>(z[i]);
  }
  return volume;
}

double depth_at(const std::vector<float>& z, const Grid& grid, std::int64_t i) {
  std::vector<float> w(z.size());
  REQUIRE(wg::hydro::fill_depressions(z, grid, w).ok());
  const auto iu = static_cast<std::size_t>(i);
  return static_cast<double>(w[iu]) - static_cast<double>(z[iu]);
}

// Land relief sum(max(0, z - 100)) in float64.
double relief(const std::vector<float>& z) {
  double r = 0.0;
  for (const float v : z) {
    r += std::max(0.0, static_cast<double>(v) - 100.0);
  }
  return r;
}

// A cone of mountains with rough flanks; the corners are ocean.
std::vector<float> rough_cone(const Grid& grid) {
  std::vector<float> z = wg::test::cone(grid, 380.0, 15.0, grid.n / 2, grid.n / 2);
  const std::vector<float> noise = wg::test::white_noise(grid, 9, "test.hydro.erosion_cone");
  for (std::size_t i = 0; i < z.size(); ++i) {
    z[i] += 40.0F * noise[i];
  }
  return z;
}

}  // namespace

TEST_CASE("hydro.erode gives the closed basin an outlet (A16 acceptance)") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> basin = wg::test::closed_basin(grid);
  const std::int64_t centre = grid.idx(16, 16);
  REQUIRE(depth_at(basin, grid, centre) == doctest::Approx(100.0));  // filled to the rim at 320
  const double before = ponded(basin, grid);
  for (const bool thermal : {false, true}) {
    CAPTURE(thermal);
    std::vector<float> z = basin;
    const ErosionReport report = run(z, grid, cycles(50, thermal));
    CHECK(report.cycles == 50);
    CHECK(report.sources_before > 0);
    CHECK(report.walker_steps > 0);
    // The rim is cut: what still ponds is a small fraction of the basin.
    CHECK(ponded(z, grid) < 0.05 * before);
    CHECK(depth_at(z, grid, centre) < 5.0);
  }
}

TEST_CASE(
    "hydro.erode lowers the land relief monotonically over a cycle sweep (A16 "
    "acceptance)") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> start = rough_cone(grid);
  for (const bool thermal : {false, true}) {
    CAPTURE(thermal);
    double previous = std::numeric_limits<double>::infinity();
    double first = 0.0;
    for (const std::int32_t count : {0, 1, 5, 20, 50}) {
      CAPTURE(count);
      std::vector<float> z = start;
      run(z, grid, cycles(count, thermal));
      const double r = relief(z);
      CHECK(r <= previous);
      if (count == 0) {
        first = r;
      }
      previous = r;
    }
    CHECK(previous < first - 100.0);
  }
}

TEST_CASE("hydro.erode follows the documented cycle layout") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> start = rough_cone(grid);
  const ErosionParams params = cycles(1, true);

  std::vector<float> z = start;
  const ErosionReport report = run(z, grid, params);

  // Cycle 0 by hand: snap, sources, one walker per source with stream (key.derive(0), source)
  // and a shared failed-basin memo, then the thermal step.
  std::vector<float> manual = start;
  REQUIRE(wg::hydro::snap_to_lattice(manual).ok());
  wg::core::Expected<std::vector<std::int32_t>> sources =
      wg::hydro::find_river_sources(manual, grid);
  REQUIRE(sources.has_value());
  CHECK(report.sources_before == static_cast<std::int32_t>(sources->size()));
  std::int64_t steps = 0;
  std::vector<std::uint8_t> visited(manual.size(), 0);
  std::vector<std::uint8_t> failed(manual.size(), 0);
  for (const std::int32_t s : *sources) {
    wg::core::RngStream stream(kKey.derive(0), static_cast<std::uint64_t>(s));
    wg::core::Expected<wg::hydro::WalkerResult> walk =
        wg::hydro::run_walker(manual, grid, s,
                              wg::hydro::WalkerParams{.dig_depth = params.dig_depth,
                                                      .step_limit = params.walker_step_limit},
                              stream, visited, {}, nullptr, failed);
    REQUIRE(walk.has_value());
    steps += walk->steps;
  }
  REQUIRE(wg::hydro::thermal_erode(
              manual, grid,
              wg::hydro::ThermalParams{.talus_threshold = params.talus_threshold,
                                       .fraction = params.talus_fraction,
                                       .max_transfers = wg::hydro::ThermalParams{}.max_transfers})
              .has_value());
  CHECK(z == manual);
  CHECK(report.walker_steps == steps);
  sources = wg::hydro::find_river_sources(manual, grid);
  REQUIRE(sources.has_value());
  CHECK(report.sources_after == static_cast<std::int32_t>(sources->size()));

  // The same key gives the same world.
  std::vector<float> again = start;
  run(again, grid, params);
  CHECK(again == z);
}

TEST_CASE("hydro.erode floods a basin without an exit once per cycle (ADR 0021)") {
  // Walkers from the rim of the closed basin run into the pit. With a step limit below the
  // basin's size the first flood fails; the memo stops every later walker of the cycle on the
  // flooded cells, so a cycle costs far fewer steps than walkers times the limit.
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = wg::test::closed_basin(grid);
  ErosionParams params = cycles(3, false);
  params.walker_step_limit = 40;
  const ErosionReport report = run(z, grid, params);
  REQUIRE(report.sources_before > 10);
  CHECK(report.walker_steps < static_cast<std::int64_t>(report.sources_before) * 40);

  // The memo is deterministic.
  std::vector<float> again = wg::test::closed_basin(grid);
  run(again, grid, params);
  CHECK(again == z);
}

TEST_CASE("hydro.erode with zero cycles only snaps to the lattice") {
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = rough_cone(grid);
  std::vector<float> snapped = z;
  REQUIRE(wg::hydro::snap_to_lattice(snapped).ok());
  const ErosionReport report = run(z, grid, cycles(0, true));
  CHECK(z == snapped);
  CHECK(report.cycles == 0);
  CHECK(report.walker_steps == 0);
  CHECK(report.sources_before == report.sources_after);
}

TEST_CASE("hydro.erode stops the walkers at the stop mask") {
  // With every cell masked the walkers stop on their sources and nothing is dug.
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = wg::test::closed_basin(grid);
  std::vector<float> snapped = z;
  REQUIRE(wg::hydro::snap_to_lattice(snapped).ok());
  const std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 1);
  wg::core::Expected<ErosionReport> report =
      wg::hydro::erode(z, grid, cycles(5, false), kKey, mask);
  REQUIRE(report.has_value());
  CHECK(report->walker_steps == 0);
  CHECK(z == snapped);
}

TEST_CASE("hydro.erode polls the cancel flag before every cycle") {
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = rough_cone(grid);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::hydro::erode(z, grid, cycles(5, true), kKey, {}, &cancel).error().code ==
        StatusCode::cancelled);
  // Without cycles there is nothing to cancel.
  CHECK(wg::hydro::erode(z, grid, cycles(0, true), kKey, {}, &cancel).has_value());
}

TEST_CASE("hydro.erode rejects invalid arguments") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::plateau(grid, 4);
  const std::vector<float> before = z;
  const auto bad = [&](const ErosionParams& p) {
    return wg::hydro::erode(z, grid, p, kKey).error().code;
  };
  ErosionParams p;
  p.erosion_cycle_count = -1;
  CHECK(bad(p) == StatusCode::invalid_argument);
  p = ErosionParams{};
  p.walker_step_limit = -1;
  CHECK(bad(p) == StatusCode::invalid_argument);
  p = ErosionParams{};
  p.dig_depth = -1.0;
  CHECK(bad(p) == StatusCode::invalid_argument);
  p = ErosionParams{};
  p.talus_threshold = std::numeric_limits<double>::quiet_NaN();
  CHECK(bad(p) == StatusCode::invalid_argument);
  p = ErosionParams{};
  p.talus_fraction = 2.0;
  CHECK(bad(p) == StatusCode::invalid_argument);
  CHECK(z == before);

  CHECK(wg::hydro::erode(z, Grid{}, ErosionParams{}, kKey).error().code ==
        StatusCode::invalid_argument);
  std::vector<float> short_z(3);
  CHECK(wg::hydro::erode(short_z, grid, ErosionParams{}, kKey).error().code ==
        StatusCode::invalid_argument);
  const std::vector<std::uint8_t> mask(2, 0);
  CHECK(wg::hydro::erode(z, grid, ErosionParams{}, kKey, mask).error().code ==
        StatusCode::invalid_argument);
  z[7] = std::numeric_limits<float>::infinity();
  CHECK(wg::hydro::erode(z, grid, ErosionParams{}, kKey).error().code ==
        StatusCode::invalid_argument);
}
