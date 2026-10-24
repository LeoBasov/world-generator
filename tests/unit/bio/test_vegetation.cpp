#include "wg/bio/vegetation.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/climate/temperature.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/water.hpp"

using wg::bio::VegetationInputs;
using wg::core::StatusCode;
using wg::params::TemperatureParams;
using wg::params::VegetationParams;

namespace {

std::vector<float> constant(const wg::core::Grid& grid, double value) {
  return wg::test::make_field(grid, [=](std::int32_t, std::int32_t) { return value; });
}

// Uniform noise in [lo, hi).
std::vector<float> noise(const wg::core::Grid& grid, std::uint64_t seed, double lo, double hi) {
  std::vector<float> v = wg::test::white_noise(grid, seed);
  for (float& x : v) {
    x = static_cast<float>(lo + ((hi - lo) * (static_cast<double>(x) + 0.5)));
  }
  return v;
}

// Constant fields of one cell type on a 17 x 17 grid.
struct Cell {
  double temperature = 25.0;
  double rainfall = 50.0;
  double elevation = 150.0;
  double drainage = 50.0;
};

std::vector<float> vegetation(const wg::core::Grid& grid, const VegetationInputs& in,
                              const VegetationParams& p = {}) {
  wg::core::SerialExecutor serial;
  std::vector<float> out(static_cast<std::size_t>(grid.cells()), -1.0F);
  REQUIRE(wg::bio::compute_vegetation(grid, in, p, TemperatureParams{}, out, serial).ok());
  return out;
}

float vegetation_of(const Cell& c, const VegetationParams& p = {},
                    std::span<const std::uint8_t> water = {}) {
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<float> t = constant(grid, c.temperature);
  const std::vector<float> rain = constant(grid, c.rainfall);
  const std::vector<float> z = constant(grid, c.elevation);
  const std::vector<float> d = constant(grid, c.drainage);
  const std::vector<float> out = vegetation(
      grid, {.temperature = t, .rainfall = rain, .elevation = z, .drainage = d, .water = water}, p);
  for (const float v : out) {
    REQUIRE(v == out.front());
  }
  return out.front();
}

}  // namespace

TEST_CASE("bio.miami_npp equals hand-computed reference values") {
  // 3000 / (1 + e^(1.315 - 0.119 T)) and 3000 (1 - e^(-0.000664 P)), evaluated by hand in
  // float64 (Lieth 1975).
  CHECK(wg::bio::miami_npp_temperature(-10.0) == doctest::Approx(226.5252118216055).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_temperature(0.0) == doctest::Approx(634.954097975703).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_temperature(10.0) == doctest::Approx(1406.3718798787313).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_temperature(25.0) == doctest::Approx(2520.714009168993).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_precipitation(0.0) == 0.0);
  CHECK(wg::bio::miami_npp_precipitation(300.0) ==
        doctest::Approx(541.8420007675072).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_precipitation(1500.0) ==
        doctest::Approx(1891.938282301104).epsilon(1e-12));
  CHECK(wg::bio::miami_npp_precipitation(3000.0) ==
        doctest::Approx(2590.7330765900574).epsilon(1e-12));
  // The scarcer factor limits.
  CHECK(wg::bio::miami_npp(10.0, 1500.0) == wg::bio::miami_npp_temperature(10.0));
  CHECK(wg::bio::miami_npp(25.0, 300.0) == wg::bio::miami_npp_precipitation(300.0));
  CHECK(wg::bio::miami_npp(-10.0, 0.0) == 0.0);
}

TEST_CASE("bio.miami_npp is monotone in T and P and bounded by 3000") {
  for (double p = 0.0; p <= 3000.0; p += 50.0) {
    double prev = -1.0;
    for (double t = -60.0; t <= 60.0; t += 0.5) {
      const double npp = wg::bio::miami_npp(t, p);
      CHECK(npp >= prev);
      CHECK(npp >= 0.0);
      CHECK(npp < wg::bio::kNppMax);
      prev = npp;
    }
  }
  for (double t = -60.0; t <= 60.0; t += 2.0) {
    double prev = -1.0;
    for (double p = 0.0; p <= 3000.0; p += 10.0) {
      const double npp = wg::bio::miami_npp(t, p);
      CHECK(npp >= prev);
      prev = npp;
    }
  }
}

TEST_CASE("bio.compute_vegetation equals hand-computed reference values") {
  // Game scale T = 2.4 c + 1 and P_mm = 30 P (defaults).
  CHECK(vegetation_of({.temperature = 25.0, .rainfall = 50.0}) ==
        doctest::Approx(0.46879062662624377).epsilon(1e-7));  // 10 degC, 1500 mm
  CHECK(vegetation_of({.temperature = 61.0, .rainfall = 100.0}) ==
        doctest::Approx(0.840238003056331).epsilon(1e-7));  // 25 degC, 3000 mm
  CHECK(vegetation_of({.temperature = 61.0, .rainfall = 10.0}) ==
        doctest::Approx(0.18061400025583574).epsilon(1e-7));  // 25 degC, 300 mm
  // Bit-exact against the scalar functions.
  const TemperatureParams tp;
  const double c = wg::climate::game_to_celsius(25.0, tp);
  CHECK(vegetation_of({}) == static_cast<float>(wg::bio::miami_npp(c, 1500.0) / 3000.0));
  CHECK(vegetation_of({.rainfall = 0.0}) == 0.0F);

  // rain_mm_per_index scales P.
  VegetationParams p;
  p.rain_mm_per_index = 60.0;
  CHECK(vegetation_of({.rainfall = 25.0}, p) == vegetation_of({.rainfall = 50.0}));
}

TEST_CASE("bio.compute_vegetation is 0 on ocean, lakes, above the tree line and on ice") {
  CHECK(vegetation_of({.elevation = 99.9}) == 0.0F);
  CHECK(vegetation_of({.elevation = 100.0}) > 0.0F);
  CHECK(vegetation_of({.elevation = 300.0}) > 0.0F);
  CHECK(vegetation_of({.elevation = 300.5}) == 0.0F);
  VegetationParams low;
  low.tree_line = 200.0;
  CHECK(vegetation_of({.elevation = 200.5}, low) == 0.0F);
  CHECK(vegetation_of({.elevation = 199.5}, low) > 0.0F);

  // Glacier (T <= -5, D >= 75) is ice; tundra keeps its Miami value.
  CHECK(wg::bio::is_ice(-5.0, 75.0));
  CHECK(!wg::bio::is_ice(-4.9, 100.0));
  CHECK(!wg::bio::is_ice(-50.0, 74.9));
  CHECK(vegetation_of({.temperature = -5.0, .drainage = 75.0}) == 0.0F);
  CHECK(vegetation_of({.temperature = -5.0, .drainage = 74.0}) > 0.0F);
  CHECK(vegetation_of({.temperature = -4.0, .drainage = 100.0}) > 0.0F);

  const wg::core::Grid grid = wg::test::make_grid(17);
  const auto cells = static_cast<std::size_t>(grid.cells());
  const std::vector<std::uint8_t> lake(cells,
                                       static_cast<std::uint8_t>(wg::hydro::WaterClass::lake));
  const std::vector<std::uint8_t> river(cells,
                                        static_cast<std::uint8_t>(wg::hydro::WaterClass::river));
  CHECK(vegetation_of({}, {}, lake) == 0.0F);
  CHECK(vegetation_of({}, {}, river) == vegetation_of({}));
}

TEST_CASE("bio.compute_vegetation stays in [0, 1] and does not depend on the thread count") {
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const wg::core::Grid grid = wg::test::make_grid(129);
  const std::vector<float> t = noise(grid, 61, -100.0, 200.0);
  const std::vector<float> rain = noise(grid, 62, 0.0, 100.0);
  const std::vector<float> z = noise(grid, 63, 0.0, 400.0);
  const std::vector<float> d = noise(grid, 64, 0.0, 100.0);
  const VegetationInputs in{.temperature = t, .rainfall = rain, .elevation = z, .drainage = d};
  const std::vector<float> out = vegetation(grid, in);
  std::vector<float> threaded(out.size());
  REQUIRE(wg::bio::compute_vegetation(grid, in, {}, {}, threaded, **pool).ok());
  CHECK(threaded == out);
  for (const float v : out) {
    CHECK(v >= 0.0F);
    CHECK(v <= 1.0F);
  }
}

TEST_CASE("bio.compute_vegetation rejects bad arguments and honours cancel") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<float> f = constant(grid, 50.0);
  const std::vector<float> short_field(static_cast<std::size_t>(grid.cells()) - 1);
  const std::vector<std::uint8_t> short_water(short_field.size());
  std::vector<float> out(static_cast<std::size_t>(grid.cells()));
  std::vector<float> short_out(short_field.size());
  const VegetationInputs ok{.temperature = f, .rainfall = f, .elevation = f, .drainage = f};
  const auto code = [&](const VegetationInputs& in, std::span<float> o) {
    return wg::bio::compute_vegetation(grid, in, {}, {}, o, serial).code;
  };
  CHECK(code(ok, out) == StatusCode::ok);
  CHECK(code({}, out) == StatusCode::invalid_argument);
  VegetationInputs bad = ok;
  bad.temperature = short_field;
  CHECK(code(bad, out) == StatusCode::invalid_argument);
  bad = ok;
  bad.rainfall = short_field;
  CHECK(code(bad, out) == StatusCode::invalid_argument);
  bad = ok;
  bad.elevation = {};
  CHECK(code(bad, out) == StatusCode::invalid_argument);
  bad = ok;
  bad.drainage = {};
  CHECK(code(bad, out) == StatusCode::invalid_argument);
  bad = ok;
  bad.water = short_water;
  CHECK(code(bad, out) == StatusCode::invalid_argument);
  CHECK(code(ok, short_out) == StatusCode::invalid_argument);
  std::vector<float> v64(64);
  CHECK(wg::bio::compute_vegetation(
            wg::test::make_grid(8),
            {.temperature = v64, .rainfall = v64, .elevation = v64, .drainage = v64}, {}, {}, v64,
            serial)
            .code == StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::bio::compute_vegetation(grid, ok, {}, {}, out, serial, &cancel).code ==
        StatusCode::cancelled);
}
