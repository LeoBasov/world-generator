// Statistical acceptance tests of A11 (plan section 9.2, estimators of section 10.1). Every test
// uses fixed seeds, so it is deterministic.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stats.hpp"
#include "wg/core/executor.hpp"
#include "wg/fractal/fractal.hpp"
#include "wg/numerics/spectrum.hpp"
#include "wg/numerics/variogram.hpp"

using wg::core::Grid;
using wg::core::RngKey;
using wg::core::SerialExecutor;
using wg::fractal::ControlGrid;
using wg::params::FieldGenParams;

namespace {

constexpr std::uint64_t kSeeds = 20;

RngKey seed_key(std::uint64_t seed) {
  return wg::core::stage_key(seed, "test.fractal.stats", 0).derive("elevation");
}

// The field before clamping.
std::vector<float> raw_field(const Grid& grid, const FieldGenParams& p, RngKey key,
                             wg::core::Executor& exec) {
  const ControlGrid c = *wg::fractal::control_grid(grid, p.mesh);
  std::vector<float> controls(static_cast<std::size_t>(wg::fractal::control_count(c)));
  REQUIRE(wg::fractal::sample_controls(p, c, key, {}, controls).ok());
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  REQUIRE(wg::fractal::diamond_square(controls, c, grid, p, key, z, exec).ok());
  return z;
}

// Mean of gamma over lags 1 ... max_lag along (step_x, step_y), summed over the seeds.
double mean_gamma(const Grid& grid, const FieldGenParams& p, std::int32_t step_x,
                  std::int32_t step_y, wg::core::Executor& exec) {
  constexpr std::int32_t kMaxLag = 4;
  double sum = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const std::vector<float> z = raw_field(grid, p, seed_key(seed), exec);
    const auto v = wg::num::directional_variogram(
        z, {}, grid, {.step_x = step_x, .step_y = step_y, .max_lag = kMaxLag}, exec);
    REQUIRE(v);
    for (const wg::num::VariogramPoint& point : *v) {
      sum += point.gamma;
    }
  }
  return sum;
}

}  // namespace

TEST_CASE("fractal.stats spectral exponent is 2H + 2 within 0.3 before clamping") {
  // N = 257, mesh 1 (the whole range is midpoint displacement), 20 seeds; the spectra are
  // averaged over the seeds and fitted over the default band (section 10.1).
  const Grid grid = wg::test::make_grid(257);
  SerialExecutor exec;
  for (const double hurst : {0.3, 0.5, 0.8}) {
    FieldGenParams p;
    p.mesh = 1;
    p.hurst = hurst;
    wg::num::RadialSpectrum mean;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      const std::vector<float> z = raw_field(grid, p, seed_key(seed), exec);
      const auto s = wg::num::radial_power_spectrum(z, grid, {});
      REQUIRE(s);
      if (mean.freq.empty()) {
        mean = *s;
        std::fill(mean.power.begin(), mean.power.end(), 0.0);
      }
      for (std::size_t b = 0; b < s->power.size(); ++b) {
        mean.power[b] += s->power[b] / static_cast<double>(kSeeds);
      }
    }
    const auto beta = wg::num::fit_spectral_exponent(mean);
    REQUIRE(beta);
    CAPTURE(hurst);
    MESSAGE("H = " << hurst << ": beta = " << *beta << ", expected " << (2.0 * hurst) + 2.0);
    CHECK(std::abs(*beta - ((2.0 * hurst) + 2.0)) <= 0.3);
  }
}

TEST_CASE("fractal.stats variogram ratio gamma_x / gamma_y rises with V_x / V_y") {
  const Grid grid = wg::test::make_grid(129);
  SerialExecutor exec;
  constexpr std::array<double, 4> kVx = {200.0, 800.0, 3200.0, 3200.0};
  constexpr std::array<double, 4> kVy = {800.0, 800.0, 800.0, 400.0};
  double previous = 0.0;
  for (std::size_t c = 0; c < kVx.size(); ++c) {
    FieldGenParams p;
    p.mesh = 1;
    p.x_variance = kVx[c];
    p.y_variance = kVy[c];
    const double ratio = mean_gamma(grid, p, 1, 0, exec) / mean_gamma(grid, p, 0, 1, exec);
    CAPTURE(kVx[c] / kVy[c]);
    MESSAGE("V_x / V_y = " << kVx[c] / kVy[c] << ": gamma_x / gamma_y = " << ratio);
    CHECK(ratio > previous);
    if (kVx[c] == kVy[c]) {
      CHECK(ratio == doctest::Approx(1.0).epsilon(0.1));
    } else if (kVx[c] > kVy[c]) {
      CHECK(ratio > 1.2);
    } else {
      CHECK(ratio < 0.8);
    }
    previous = ratio;
  }
}

TEST_CASE("fractal.stats control values pass chi2 against the weights") {
  // Mesh 6 on N = 257: 33^2 control points per seed, 20 seeds.
  const Grid grid = wg::test::make_grid(257);
  FieldGenParams p;
  p.min = 1.0;
  p.max = 401.0;
  p.mesh = 6;
  p.weights = {60.0, 10.0, 10.0, 10.0, 10.0};
  const ControlGrid c = *wg::fractal::control_grid(grid, p.mesh);
  std::vector<float> values(static_cast<std::size_t>(wg::fractal::control_count(c)));
  std::array<double, 5> observed{};
  double total = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    REQUIRE(wg::fractal::sample_controls(p, c, seed_key(seed), {}, values).ok());
    for (const float v : values) {
      const auto bin =
          static_cast<std::size_t>(std::clamp((static_cast<double>(v) - p.min) / 80.0, 0.0, 4.0));
      observed[bin] += 1.0;
      total += 1.0;
    }
  }
  std::array<double, 5> expected{};
  for (std::size_t b = 0; b < 5; ++b) {
    expected[b] = total * p.weights[b] / 100.0;
  }
  const double chi2 = wg::test::chi2_statistic(expected, observed);
  MESSAGE("chi2 = " << chi2 << " over " << total << " control values");
  CHECK(chi2 < wg::test::chi2_critical(4));
}

TEST_CASE("fractal.stats values agree for N = 129 and 257 at a fixed mesh") {
  // Two-sample KS of the values of 20 seeds. The values are spatially correlated, so the iid
  // critical value does not apply; the bound is the tolerance of ADR 0009.
  constexpr double kTolerance = 0.05;
  FieldGenParams p;
  p.min = 1.0;
  p.max = 400.0;
  p.mesh = 3;
  SerialExecutor exec;
  std::vector<double> small;
  std::vector<double> large;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    for (const std::int32_t n : {129, 257}) {
      const Grid grid = wg::test::make_grid(n);
      std::vector<float> z(static_cast<std::size_t>(grid.cells()));
      REQUIRE(wg::fractal::generate_field(grid, p, seed_key(seed), {}, z, exec).ok());
      std::vector<double>& out = n == 129 ? small : large;
      out.insert(out.end(), z.begin(), z.end());
    }
  }
  const double d = wg::test::ks_two_sample(small, large);
  MESSAGE("KS D(129, 257) = " << d);
  CHECK(d < kTolerance);
}
