#include "wg/climate/temperature.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "support/fixtures.hpp"
#include "support/source_files.hpp"
#include "wg/climate/latitude.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/params/tables.hpp"

using wg::climate::TemperatureInputs;
using wg::core::StatusCode;
using wg::params::TemperatureParams;

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

std::vector<float> temperature(const wg::core::Grid& grid, const TemperatureInputs& in,
                               const TemperatureParams& p, wg::core::Executor& exec) {
  std::vector<float> out(static_cast<std::size_t>(grid.cells()), -1.0F);
  REQUIRE(wg::climate::compute_temperature(grid, in, p, out, exec).ok());
  return out;
}

std::size_t at(const wg::core::Grid& grid, std::int32_t x, std::int32_t y) {
  return static_cast<std::size_t>(grid.idx(x, y));
}

}  // namespace

TEST_CASE("climate.lapse_term is -k Gamma h_u per unit above sea level and zero below") {
  CHECK(wg::climate::lapse_term(200.0, 6.5, 10.0, 2.4) ==
        doctest::Approx(-2.4 * 6.5 * 100.0 * 10.0 / 1000.0).epsilon(1e-15));
  CHECK(wg::climate::lapse_term(101.0, 8.0, 125.0, 1.0) == -1.0);
  for (const double z : {0.0, 50.0, 99.0, 99.999, 100.0}) {
    CAPTURE(z);
    CHECK(wg::climate::lapse_term(z, 6.5, 10.0, 2.4) == 0.0);
  }
  CHECK(wg::climate::lapse_term(100.001, 6.5, 10.0, 2.4) < 0.0);
  CHECK(wg::climate::lapse_term(300.0, 0.0, 10.0, 2.4) == 0.0);
}

TEST_CASE("climate.compute_temperature: a ramp gives dT/dz = -Gamma h_u") {
  wg::core::SerialExecutor serial;
  SUBCASE("dyadic parameters: exactly -1 per elevation unit") {
    // k Gamma h_u / 1000 = 1 * 8 * 125 / 1000 = 1 game unit per elevation unit.
    const wg::core::Grid grid = wg::test::make_grid(33, 100.0, 125.0);
    TemperatureParams p;
    p.lapse_rate = 8.0;
    p.game_per_kelvin = 1.0;
    const std::vector<float> base = constant(grid, 50.0);
    const std::vector<float> z = wg::test::ramp(grid, 100.0, 1.0);
    const std::vector<float> t = temperature(grid, {.base = base, .elevation = z}, p, serial);
    for (std::int32_t y = 0; y < grid.n; ++y) {
      for (std::int32_t x = 0; x < grid.n; ++x) {
        REQUIRE(t[at(grid, x, y)] == static_cast<float>(50 - x));
        if (x > 0) {
          REQUIRE(t[at(grid, x, y)] - t[at(grid, x - 1, y)] == -1.0F);
        }
      }
    }
  }
  SUBCASE("default parameters: -k Gamma h_u / 1000 within float rounding") {
    const wg::core::Grid grid = wg::test::make_grid(33, 100.0, 10.0);
    const TemperatureParams p;
    const double slope = -p.game_per_kelvin * p.lapse_rate * grid.hu_m / 1000.0;
    const double g = 7.5;
    const std::vector<float> base = constant(grid, 60.0);
    const std::vector<float> z = wg::test::ramp(grid, 100.0, g);
    const std::vector<float> t = temperature(grid, {.base = base, .elevation = z}, p, serial);
    for (std::int32_t x = 1; x < grid.n; ++x) {
      CAPTURE(x);
      const double d = static_cast<double>(t[at(grid, x, 3)]) - t[at(grid, x - 1, 3)];
      CHECK(d == doctest::Approx(slope * g).epsilon(1e-4));
      CHECK(static_cast<double>(t[at(grid, x, 3)]) ==
            doctest::Approx(60.0 + slope * g * x).epsilon(1e-6));
    }
  }
}

TEST_CASE("climate.compute_temperature: latitude term equals -Delta T sin^2(phi)") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(65);
  const TemperatureParams p;
  const std::vector<float> base = constant(grid, 40.0);
  for (const wg::params::Poles poles : {wg::params::Poles::none, wg::params::Poles::north,
                                        wg::params::Poles::south, wg::params::Poles::both}) {
    CAPTURE(static_cast<int>(poles));
    std::vector<float> phi(static_cast<std::size_t>(grid.cells()));
    REQUIRE(wg::climate::fill_latitude(poles, grid, phi, serial).ok());
    const std::vector<float> t = temperature(grid, {.base = base, .latitude = phi}, p, serial);
    for (std::size_t i = 0; i < t.size(); ++i) {
      const double s = std::sin(static_cast<double>(phi[i]));
      REQUIRE(std::abs(static_cast<double>(t[i]) - (40.0 - p.equator_pole_delta * s * s)) < 1e-4);
    }
  }
}

TEST_CASE("climate.compute_temperature: no lapse term at or below sea level") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17, 100.0, 10.0);
  const TemperatureParams p;
  const std::vector<float> base = noise(grid, 7, 0.0, 100.0);
  for (const double level : {0.0, 50.0, 99.0, 100.0}) {
    CAPTURE(level);
    const std::vector<float> z = constant(grid, level);
    CHECK(temperature(grid, {.base = base, .elevation = z}, p, serial) == base);
  }
  const std::vector<float> above = constant(grid, 100.5);
  const std::vector<float> t = temperature(grid, {.base = base, .elevation = above}, p, serial);
  for (std::size_t i = 0; i < t.size(); ++i) {
    REQUIRE(t[i] < base[i]);
  }
}

TEST_CASE("climate.compute_temperature: V_veg = 1 lowers T by exactly alpha") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<float> base = wg::test::make_field(
      grid, [](std::int32_t x, std::int32_t y) { return 10.0 + x + (0.25 * y); });
  const std::vector<float> none = constant(grid, 0.0);
  const std::vector<float> full = constant(grid, 1.0);
  for (const double alpha : {10.0, 0.5, 0.0}) {
    CAPTURE(alpha);
    TemperatureParams p;
    p.forest_damping = alpha;
    const std::vector<float> t0 = temperature(grid, {.base = base, .vegetation = none}, p, serial);
    const std::vector<float> t1 = temperature(grid, {.base = base, .vegetation = full}, p, serial);
    CHECK(t0 == base);
    for (std::size_t i = 0; i < t0.size(); ++i) {
      REQUIRE(t0[i] - t1[i] == static_cast<float>(alpha));
    }
  }
  CHECK(wg::climate::vegetation_term(0.5, 10.0) == -5.0);
}

TEST_CASE("climate.compute_temperature: rain damping") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  TemperatureParams p;
  p.rain_damping = 6.0;
  const std::vector<float> base = constant(grid, 30.0);
  const std::vector<float> dry = constant(grid, 0.0);
  const std::vector<float> wet = constant(grid, 100.0);
  CHECK(temperature(grid, {.base = base, .rainfall = dry}, p, serial) == base);
  CHECK(temperature(grid, {.base = base, .rainfall = wet}, p, serial) == constant(grid, 24.0));
  CHECK(wg::climate::rain_term(50.0, 6.0) == -3.0);
}

TEST_CASE("climate.compute_temperature: all terms, clamp, in place and thread count") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const wg::core::Grid grid = wg::test::make_grid(65, 100.0, 10.0);
  const TemperatureParams p;
  std::vector<float> phi(static_cast<std::size_t>(grid.cells()));
  REQUIRE(wg::climate::fill_latitude(wg::params::Poles::both, grid, phi, serial).ok());
  const std::vector<float> base = noise(grid, 1, 25.0, 75.0);
  const std::vector<float> z = noise(grid, 2, 0.0, 400.0);
  const std::vector<float> v = noise(grid, 3, 0.0, 1.0);
  const std::vector<float> rain = noise(grid, 4, 0.0, 100.0);
  const TemperatureInputs in{
      .base = base, .latitude = phi, .elevation = z, .vegetation = v, .rainfall = rain};

  const std::vector<float> t = temperature(grid, in, p, serial);
  for (std::size_t i = 0; i < t.size(); ++i) {
    auto expected = static_cast<double>(base[i]);
    expected += wg::climate::latitude_term(static_cast<double>(phi[i]), p.equator_pole_delta);
    expected +=
        wg::climate::lapse_term(static_cast<double>(z[i]), p.lapse_rate, 10.0, p.game_per_kelvin);
    expected += wg::climate::vegetation_term(static_cast<double>(v[i]), p.forest_damping);
    expected += wg::climate::rain_term(static_cast<double>(rain[i]), p.rain_damping);
    REQUIRE(t[i] == static_cast<float>(expected));
  }
  CHECK(temperature(grid, in, p, **pool) == t);

  // In place: out is the base span.
  std::vector<float> inplace = base;
  TemperatureInputs alias = in;
  alias.base = inplace;
  REQUIRE(wg::climate::compute_temperature(grid, alias, p, inplace, **pool).ok());
  CHECK(inplace == t);

  // The sum is kept inside the registry range [-1000, 1000].
  const std::vector<float> hot = constant(grid, 990.0);
  const std::vector<float> cold = constant(grid, -995.0);
  const std::vector<float> wet = constant(grid, 100.0);
  TemperatureParams q;
  q.rain_damping = 50.0;
  CHECK(temperature(grid, {.base = hot}, q, serial) == hot);
  const std::vector<float> clamped = temperature(grid, {.base = cold, .rainfall = wet}, q, serial);
  CHECK(clamped == constant(grid, -1000.0));
  CHECK(temperature(grid, {.base = constant(grid, 1500.0)}, q, serial) == constant(grid, 1000.0));
}

TEST_CASE("climate.compute_temperature rejects bad arguments and honours cancel") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const TemperatureParams p;
  const std::vector<float> base = constant(grid, 0.0);
  const std::vector<float> short_field(static_cast<std::size_t>(grid.cells()) - 1);
  std::vector<float> out(static_cast<std::size_t>(grid.cells()));
  CHECK(wg::climate::compute_temperature(grid, {}, p, out, serial).code ==
        StatusCode::invalid_argument);
  for (std::size_t which = 0; which < 4; ++which) {
    CAPTURE(which);
    TemperatureInputs in{.base = base};
    std::array<std::span<const float>*, 4> optional = {&in.latitude, &in.elevation, &in.vegetation,
                                                       &in.rainfall};
    *optional[which] = short_field;
    CHECK(wg::climate::compute_temperature(grid, in, p, out, serial).code ==
          StatusCode::invalid_argument);
  }
  std::vector<float> short_out(short_field.size());
  CHECK(wg::climate::compute_temperature(grid, {.base = base}, p, short_out, serial).code ==
        StatusCode::invalid_argument);
  std::vector<float> bad(64);
  CHECK(wg::climate::compute_temperature(wg::test::make_grid(8), {.base = bad}, p, bad, serial)
            .code == StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::climate::compute_temperature(grid, {.base = base}, p, out, serial, &cancel).code ==
        StatusCode::cancelled);
}

TEST_CASE("climate.celsius_to_game matches the shipped climate classes") {
  const TemperatureParams p;
  for (const double c : {-40.0, 0.0, 12.5, 37.0}) {
    CHECK(wg::climate::game_to_celsius(wg::climate::celsius_to_game(c, p), p) ==
          doctest::Approx(c).epsilon(1e-12));
  }
  CHECK(wg::climate::celsius_to_game(0.0, p) == p.celsius_offset);

  const auto classes = wg::params::load_climate_classes(
      wg::test::read_source_file("data/tables/climate_classes.json"));
  REQUIRE(classes);
  REQUIRE(!classes->classes.empty());
  for (std::size_t i = 0; i < classes->classes.size(); ++i) {
    const wg::params::ClimateClass& c = classes->classes[i];
    CAPTURE(c.name);
    const double mid = (c.celsius_min + c.celsius_max) / 2.0;
    CHECK(classes->classify(wg::climate::celsius_to_game(mid, p)) == i);
  }
}
