#include "wg/numerics/spectrum.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"

using wg::core::StatusCode;
using wg::num::RadialSpectrum;

namespace {

constexpr double kPi = std::numbers::pi;

}  // namespace

TEST_CASE("numerics.spectrum of white noise is flat within 10 percent") {
  // Mean over 8 fields of uniform noise (variance 1/12), N = 257, in octave bands of radius
  // k: single bins hold too few independent modes for a 10 % bound.
  const wg::core::Grid grid = wg::test::make_grid(257);
  constexpr double kVariance = 1.0 / 12.0;
  constexpr std::uint64_t kSeeds = 8;
  RadialSpectrum mean;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const std::vector<float> z = wg::test::white_noise(grid, seed, "test.numerics.spectrum");
    const wg::core::Expected<RadialSpectrum> s = wg::num::radial_power_spectrum(z, grid, {});
    REQUIRE(s);
    REQUIRE(s->freq.size() == 128);
    if (mean.freq.empty()) {
      mean = *s;
      std::fill(mean.power.begin(), mean.power.end(), 0.0);
    }
    for (std::size_t b = 0; b < s->power.size(); ++b) {
      mean.power[b] += s->power[b] / static_cast<double>(kSeeds);
    }
  }
  CHECK(mean.freq.front() == 1.0 / 256.0);
  CHECK(mean.freq.back() == 0.5);
  for (std::size_t lo = 2; lo < 128; lo *= 2) {
    double power = 0.0;
    double modes = 0.0;
    for (std::size_t k = lo; k < 2 * lo; ++k) {
      power += mean.power[k - 1] * static_cast<double>(mean.modes[k - 1]);
      modes += static_cast<double>(mean.modes[k - 1]);
    }
    const double ratio = power / modes / kVariance;
    CAPTURE(lo);
    CAPTURE(ratio);
    CHECK(ratio > 0.9);
    CHECK(ratio < 1.1);
  }
  const wg::core::Expected<double> beta = wg::num::fit_spectral_exponent(mean);
  REQUIRE(beta);
  CHECK(std::abs(*beta) < 0.1);
}

TEST_CASE("numerics.spectrum of a sinusoid peaks at its frequency") {
  const wg::core::Grid grid = wg::test::make_grid(129);
  const auto wave = [](std::int32_t x, std::int32_t /*y*/) {
    return std::cos(2.0 * kPi * 16.0 * static_cast<double>(x) / 128.0);
  };
  const std::vector<float> z = wg::test::make_field(grid, wave);
  for (const bool hann : {true, false}) {
    CAPTURE(hann);
    const wg::core::Expected<RadialSpectrum> s =
        wg::num::radial_power_spectrum(z, grid, {.hann = hann});
    REQUIRE(s);
    const auto peak = std::distance(s->power.begin(),
                                    std::max_element(s->power.begin(), s->power.end()));
    CHECK(peak == 15);  // bin of k = 16
    CHECK(s->freq[15] == 16.0 / 128.0);
  }
}

TEST_CASE("numerics.spectrum fit recovers the exponent of a power law") {
  RadialSpectrum s;
  for (std::int32_t k = 1; k <= 64; ++k) {
    const double f = k / 128.0;
    s.freq.push_back(f);
    s.power.push_back(3.0 * std::pow(f, -2.5));
    s.modes.push_back(1);
  }
  const wg::core::Expected<double> beta = wg::num::fit_spectral_exponent(s);
  REQUIRE(beta);
  CHECK(*beta == doctest::Approx(2.5).epsilon(1e-12));
  const wg::core::Expected<double> band =
      wg::num::fit_spectral_exponent(s, {.f_min = 0.1, .f_max = 0.2});
  REQUIRE(band);
  CHECK(*band == doctest::Approx(2.5).epsilon(1e-12));

  CHECK(wg::num::fit_spectral_exponent(s, {.f_min = 0.3, .f_max = 0.2}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::fit_spectral_exponent(s, {.f_min = 0.1, .f_max = 0.1}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::fit_spectral_exponent(RadialSpectrum{}).error().code ==
        StatusCode::invalid_argument);
  s.modes.pop_back();
  CHECK(wg::num::fit_spectral_exponent(s).error().code == StatusCode::invalid_argument);
}

TEST_CASE("numerics.spectrum rejects bad input") {
  const wg::core::Grid grid = wg::test::make_grid(9);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 1.0F);
  const std::vector<float> short_z(3);
  CHECK(wg::num::radial_power_spectrum(short_z, grid, {}).error().code ==
        StatusCode::invalid_argument);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::radial_power_spectrum(z, grid, {}, resource).error().code ==
        StatusCode::out_of_memory);
  z[3] = std::numeric_limits<float>::infinity();
  CHECK(wg::num::radial_power_spectrum(z, grid, {}).error().code == StatusCode::invalid_argument);
}
