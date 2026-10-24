#include "wg/climate/precipitation.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::climate::PrecipitationInputs;
using wg::core::StatusCode;
using wg::params::PrecipitationParams;

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

PrecipitationParams no_quirk() {
  PrecipitationParams p;
  p.df_drainage_quirk = false;
  return p;
}

struct Result {
  std::vector<float> rain;
  std::vector<float> q;
};

Result precipitation(const wg::core::Grid& grid, const PrecipitationInputs& in,
                     const PrecipitationParams& p, wg::core::Executor& exec) {
  Result r{std::vector<float>(static_cast<std::size_t>(grid.cells()), -1.0F),
           std::vector<float>(static_cast<std::size_t>(grid.cells()), -1.0F)};
  REQUIRE(wg::climate::orographic_precipitation(grid, in, p, r.rain, r.q, exec).ok());
  return r;
}

// Straightforward serial transcription of the R7.2 sweep (ADR 0016), one cell at a time.
Result reference(const wg::core::Grid& grid, const PrecipitationInputs& in,
                 const PrecipitationParams& p) {
  Result r{std::vector<float>(static_cast<std::size_t>(grid.cells())),
           std::vector<float>(static_cast<std::size_t>(grid.cells()))};
  for (std::int32_t y = 0; y < grid.n; ++y) {
    double q = p.q_max;
    for (std::int32_t x = 0; x < grid.n; ++x) {
      const auto k = static_cast<std::size_t>(grid.idx(x, y));
      const double z = in.elevation[k];
      double rain = in.base[k];
      if (p.df_drainage_quirk && in.drainage[k] < 50.0F) {
        r.rain[k] = in.base[k];
      } else {
        rain = in.base[k] * (q / p.q_max);
        if (x > 0) {
          const double up = std::max(static_cast<double>(in.elevation[k - 1]), 100.0);
          rain += p.orographic_gain * q * std::max(0.0, std::max(z, 100.0) - up);
        }
        rain = std::min(100.0, std::max(0.0, rain));
        r.rain[k] = static_cast<float>(rain);
      }
      q = q - (p.draw_fraction * rain) + (z < 100.0 ? p.recharge_ocean : p.recharge_land);
      q = std::min(p.q_max, std::max(0.0, q));
      r.q[k] = static_cast<float>(q);
    }
  }
  return r;
}

std::size_t at(const wg::core::Grid& grid, std::int32_t x, std::int32_t y) {
  return static_cast<std::size_t>(grid.idx(x, y));
}

// Ocean for x < 8 and x >= 48, land with a Gaussian ridge along x = 24 in between.
constexpr std::int32_t kCoastWest = 8;
constexpr std::int32_t kCrest = 24;
constexpr std::int32_t kCoastEast = 48;

std::vector<float> ridge_island(const wg::core::Grid& grid) {
  std::vector<float> z = wg::test::gaussian_ridge(grid, kCrest, 4.0, 60.0, 101.0);
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      if (x < kCoastWest || x >= kCoastEast) {
        z[at(grid, x, y)] = 50.0F;
      }
    }
  }
  return z;
}

}  // namespace

TEST_CASE("climate.orographic_precipitation: flat saturated terrain gives P = P_base") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> base = noise(grid, 11, 0.0, 100.0);
  SUBCASE("land whose recharge covers the largest draw") {
    PrecipitationParams p = no_quirk();
    p.recharge_land = p.draw_fraction * 100.0;
    const Result r =
        precipitation(grid, {.base = base, .elevation = constant(grid, 150.0)}, p, serial);
    CHECK(r.rain == base);
    CHECK(r.q == constant(grid, p.q_max));
  }
  SUBCASE("ocean with the default parameters") {
    const Result r =
        precipitation(grid, {.base = base, .elevation = constant(grid, 50.0)}, no_quirk(), serial);
    CHECK(r.rain == base);
  }
  SUBCASE("land with the default recharge dries out downwind") {
    const PrecipitationParams p = no_quirk();
    const std::vector<float> wet = constant(grid, 80.0);
    const Result r =
        precipitation(grid, {.base = wet, .elevation = constant(grid, 150.0)}, p, serial);
    for (std::int32_t y = 0; y < grid.n; ++y) {
      CHECK(r.rain[at(grid, 0, y)] == 80.0F);
      for (std::int32_t x = 1; x < grid.n; ++x) {
        REQUIRE(r.rain[at(grid, x, y)] < r.rain[at(grid, x - 1, y)]);
        REQUIRE(r.q[at(grid, x, y)] < r.q[at(grid, x - 1, y)]);
      }
    }
  }
}

TEST_CASE("climate.orographic_precipitation: ridge gives a windward maximum and a lee shadow") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(65);
  PrecipitationParams p = no_quirk();
  p.orographic_gain = 0.01;
  const double p_base = 40.0;
  const std::vector<float> base = constant(grid, p_base);
  const std::vector<float> z = ridge_island(grid);
  const Result r = precipitation(grid, {.base = base, .elevation = z}, p, serial);

  for (std::int32_t y = 0; y < grid.n; ++y) {
    CAPTURE(y);
    const auto row =
        std::span<const float>(r.rain).subspan(at(grid, 0, y), static_cast<std::size_t>(grid.n));
    const auto qrow =
        std::span<const float>(r.q).subspan(at(grid, 0, y), static_cast<std::size_t>(grid.n));
    // Saturated air over the western ocean.
    for (std::int32_t x = 0; x < kCoastWest; ++x) {
      REQUIRE(row[static_cast<std::size_t>(x)] == static_cast<float>(p_base));
    }
    // The wettest cell lies on the windward slope, above the base rainfall.
    const auto wettest =
        static_cast<std::int32_t>(std::max_element(row.begin(), row.end()) - row.begin());
    CHECK(wettest >= kCoastWest);
    CHECK(wettest < kCrest);
    CHECK(row[static_cast<std::size_t>(wettest)] > static_cast<float>(p_base));
    // Each windward cell is wetter than its mirror image on the lee side.
    for (std::int32_t k = 1; k <= 8; ++k) {
      REQUIRE(row[static_cast<std::size_t>(kCrest - k)] >
              row[static_cast<std::size_t>(kCrest + k)]);
    }
    // Rain shadow: the whole lee side is drier than the base rainfall.
    for (std::int32_t x = kCrest + 1; x < kCoastEast; ++x) {
      REQUIRE(row[static_cast<std::size_t>(x)] < static_cast<float>(p_base));
    }
    // Recovery over the eastern ocean: q gains at least E_ocean - eta 100 per tile.
    const double deficit = p.q_max - qrow[static_cast<std::size_t>(kCoastEast - 1)];
    REQUIRE(deficit > 0.0);
    const auto tiles = static_cast<std::int32_t>(
        std::ceil(deficit / (p.recharge_ocean - (p.draw_fraction * 100.0))));
    REQUIRE(kCoastEast + tiles < grid.n);
    for (std::int32_t x = kCoastEast + tiles - 1; x < grid.n; ++x) {
      REQUIRE(qrow[static_cast<std::size_t>(x)] == static_cast<float>(p.q_max));
    }
    for (std::int32_t x = kCoastEast + tiles; x < grid.n; ++x) {
      REQUIRE(row[static_cast<std::size_t>(x)] == static_cast<float>(p_base));
    }
  }
}

TEST_CASE("climate.orographic_precipitation matches the R7.2 recurrence") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> base = noise(grid, 21, 0.0, 100.0);
  const std::vector<float> z = noise(grid, 22, 0.0, 400.0);
  const std::vector<float> d = noise(grid, 23, 0.0, 100.0);
  for (const bool quirk : {false, true}) {
    CAPTURE(quirk);
    PrecipitationParams p;
    p.df_drainage_quirk = quirk;
    p.orographic_gain = 0.003;
    const PrecipitationInputs in{.base = base, .elevation = z, .drainage = d};
    const Result r = precipitation(grid, in, p, serial);
    const Result expected = reference(grid, in, p);
    CHECK(r.rain == expected.rain);
    CHECK(r.q == expected.q);
  }
}

TEST_CASE("climate.orographic_precipitation keeps 0 <= q <= q_max and 0 <= P <= 100") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<float> base = noise(grid, 31, 0.0, 100.0);
  const std::vector<float> z = noise(grid, 32, 0.0, 400.0);
  struct Case {
    double q_max, gain, eta, e_ocean, e_land;
  };
  const std::array<Case, 5> cases = {{{100.0, 0.5, 0.02, 10.0, 0.5},
                                      {0.001, 100.0, 1.0, 1000.0, 0.0},
                                      {1000.0, 5.0, 0.5, 0.0, 0.0},
                                      {50.0, 0.0, 0.0, 0.0, 0.0},
                                      {1.0, 0.0, 1.0, 1000.0, 1000.0}}};
  for (std::size_t c = 0; c < cases.size(); ++c) {
    CAPTURE(c);
    PrecipitationParams p = no_quirk();
    p.q_max = cases[c].q_max;
    p.orographic_gain = cases[c].gain;
    p.draw_fraction = cases[c].eta;
    p.recharge_ocean = cases[c].e_ocean;
    p.recharge_land = cases[c].e_land;
    const Result r = precipitation(grid, {.base = base, .elevation = z}, p, serial);
    for (std::size_t i = 0; i < r.q.size(); ++i) {
      REQUIRE(r.q[i] >= 0.0F);
      REQUIRE(r.q[i] <= static_cast<float>(p.q_max));
      REQUIRE(r.rain[i] >= 0.0F);
      REQUIRE(r.rain[i] <= 100.0F);
    }
  }
}

TEST_CASE("climate.orographic_precipitation: toggle off leaves P unchanged") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<float> base = noise(grid, 41, 0.0, 100.0);
  const std::vector<float> z = ridge_island(grid);
  PrecipitationParams p;  // quirk on, but no drainage is needed when the toggle is off
  p.orographic_precipitation = false;
  const Result r = precipitation(grid, {.base = base, .elevation = z}, p, serial);
  CHECK(r.rain == base);
  CHECK(r.q == constant(grid, -1.0));
  CHECK(precipitation(grid, {.base = base, .elevation = z}, no_quirk(), serial).rain != base);
}

TEST_CASE("climate.orographic_precipitation: DF drainage quirk") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<float> base = constant(grid, 40.0);
  const std::vector<float> z = ridge_island(grid);
  PrecipitationParams p;
  REQUIRE(p.df_drainage_quirk);
  p.orographic_gain = 0.01;

  SUBCASE("cells with D < 50 keep P_base, the others follow the sweep") {
    const std::vector<float> d = wg::test::make_field(
        grid, [](std::int32_t x, std::int32_t) { return x % 2 == 0 ? 30.0 : 70.0; });
    const Result r = precipitation(grid, {.base = base, .elevation = z, .drainage = d}, p, serial);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < r.rain.size(); ++i) {
      if (d[i] < 50.0F) {
        REQUIRE(r.rain[i] == base[i]);
      } else if (r.rain[i] != base[i]) {
        ++changed;
      }
    }
    CHECK(changed > 0);
  }
  SUBCASE("D >= 50 everywhere equals the quirk off") {
    const std::vector<float> d = constant(grid, 50.0);
    const Result on = precipitation(grid, {.base = base, .elevation = z, .drainage = d}, p, serial);
    PrecipitationParams off = p;
    off.df_drainage_quirk = false;
    const Result r = precipitation(grid, {.base = base, .elevation = z}, off, serial);
    CHECK(on.rain == r.rain);
    CHECK(on.q == r.q);
  }
  SUBCASE("D < 50 everywhere keeps P_base but still depletes the column") {
    const std::vector<float> d = constant(grid, 49.0);
    const Result r = precipitation(grid, {.base = base, .elevation = z, .drainage = d}, p, serial);
    CHECK(r.rain == base);
    CHECK(r.q[at(grid, kCoastEast - 1, 0)] < static_cast<float>(p.q_max));
  }
}

TEST_CASE("climate.orographic_precipitation: in place and thread count") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const wg::core::Grid grid = wg::test::make_grid(129);
  const std::vector<float> base = noise(grid, 51, 0.0, 100.0);
  const std::vector<float> z = noise(grid, 52, 0.0, 400.0);
  const std::vector<float> d = noise(grid, 53, 0.0, 100.0);
  PrecipitationParams p;
  p.orographic_gain = 0.003;
  const PrecipitationInputs in{.base = base, .elevation = z, .drainage = d};
  const Result r = precipitation(grid, in, p, serial);
  const Result threaded = precipitation(grid, in, p, **pool);
  CHECK(threaded.rain == r.rain);
  CHECK(threaded.q == r.q);

  std::vector<float> inplace = base;
  PrecipitationInputs alias = in;
  alias.base = inplace;
  REQUIRE(wg::climate::orographic_precipitation(grid, alias, p, inplace, {}, **pool).ok());
  CHECK(inplace == r.rain);
}

TEST_CASE("climate.orographic_precipitation rejects bad arguments and honours cancel") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const PrecipitationParams p = no_quirk();
  const std::vector<float> base = constant(grid, 10.0);
  const std::vector<float> z = constant(grid, 150.0);
  const std::vector<float> short_field(static_cast<std::size_t>(grid.cells()) - 1);
  std::vector<float> out(static_cast<std::size_t>(grid.cells()));
  std::vector<float> short_out(short_field.size());
  const auto code = [&](const PrecipitationInputs& in, const PrecipitationParams& params,
                        std::span<float> o, std::span<float> q) {
    return wg::climate::orographic_precipitation(grid, in, params, o, q, serial).code;
  };
  CHECK(code({}, p, out, {}) == StatusCode::invalid_argument);
  CHECK(code({.base = base}, p, out, {}) == StatusCode::invalid_argument);
  CHECK(code({.base = short_field, .elevation = z}, p, out, {}) == StatusCode::invalid_argument);
  CHECK(code({.base = base, .elevation = short_field}, p, out, {}) == StatusCode::invalid_argument);
  CHECK(code({.base = base, .elevation = z, .drainage = short_field}, p, out, {}) ==
        StatusCode::invalid_argument);
  CHECK(code({.base = base, .elevation = z}, p, short_out, {}) == StatusCode::invalid_argument);
  CHECK(code({.base = base, .elevation = z}, p, out, short_out) == StatusCode::invalid_argument);
  CHECK(code({.base = base, .elevation = z}, PrecipitationParams{}, out, {}) ==
        StatusCode::invalid_argument);
  std::vector<float> bad(64);
  CHECK(wg::climate::orographic_precipitation(wg::test::make_grid(8),
                                              {.base = bad, .elevation = bad}, p, bad, {}, serial)
            .code == StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::climate::orographic_precipitation(grid, {.base = base, .elevation = z}, p, out, {},
                                              serial, &cancel)
            .code == StatusCode::cancelled);
}
