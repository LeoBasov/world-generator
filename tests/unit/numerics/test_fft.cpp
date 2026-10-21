#include "wg/numerics/fft.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "support/counting_resource.hpp"
#include "wg/core/rng.hpp"

using wg::core::StatusCode;
using wg::num::Complex;

static_assert(wg::num::is_power_of_two(1));
static_assert(wg::num::is_power_of_two(256));
static_assert(!wg::num::is_power_of_two(0));
static_assert(!wg::num::is_power_of_two(257));

namespace {

constexpr double kPi = std::numbers::pi;
constexpr std::int32_t kRows = 4;  // of the 2-D test
constexpr std::int32_t kCols = 8;

std::vector<Complex> random_signal(std::size_t n, std::uint64_t seed) {
  const wg::core::RngKey key = wg::core::stage_key(seed, "test.numerics.fft", 0);
  std::vector<Complex> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    x[i] = Complex{wg::core::uniform01(key, i, 0) - 0.5, wg::core::uniform01(key, i, 1) - 0.5};
  }
  return x;
}

// X_k = sum_j x_j exp(-2 pi i j k / N), evaluated directly.
std::vector<Complex> naive_dft(const std::vector<Complex>& x) {
  const std::size_t n = x.size();
  std::vector<Complex> out(n, Complex{0.0, 0.0});
  for (std::size_t k = 0; k < n; ++k) {
    for (std::size_t j = 0; j < n; ++j) {
      const double a = -2.0 * kPi * static_cast<double>((j * k) % n) / static_cast<double>(n);
      out[k].re += (x[j].re * std::cos(a)) - (x[j].im * std::sin(a));
      out[k].im += (x[j].re * std::sin(a)) + (x[j].im * std::cos(a));
    }
  }
  return out;
}

double max_error(const std::vector<Complex>& a, const std::vector<Complex>& b) {
  double e = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    e = std::fmax(e, std::fmax(std::abs(a[i].re - b[i].re), std::abs(a[i].im - b[i].im)));
  }
  return e;
}

}  // namespace

TEST_CASE("numerics.fft matches the direct DFT and inverts") {
  for (const std::size_t n : {1U, 2U, 4U, 8U, 64U, 256U}) {
    CAPTURE(n);
    const std::vector<Complex> x = random_signal(n, n);
    std::vector<Complex> f = x;
    REQUIRE(wg::num::fft(f).ok());
    CHECK(max_error(f, naive_dft(x)) < 1e-10);
    REQUIRE(wg::num::fft(f, true).ok());
    CHECK(max_error(f, x) < 1e-12);
  }
}

TEST_CASE("numerics.fft 2-D transform matches the direct 2-D DFT") {
  const std::vector<Complex> x = random_signal(static_cast<std::size_t>(kRows) * kCols, 3);
  std::vector<Complex> f = x;
  REQUIRE(wg::num::fft_2d(f, kRows, kCols).ok());
  const auto at = [](std::int32_t row, std::int32_t col) {
    return (static_cast<std::size_t>(row) * kCols) + static_cast<std::size_t>(col);
  };
  for (std::int32_t ky = 0; ky < kRows; ++ky) {
    for (std::int32_t kx = 0; kx < kCols; ++kx) {
      double re = 0.0;
      double im = 0.0;
      for (std::int32_t y = 0; y < kRows; ++y) {
        for (std::int32_t xx = 0; xx < kCols; ++xx) {
          const double fy = static_cast<double>(ky) * y / kRows;
          const double fx = static_cast<double>(kx) * xx / kCols;
          const double a = -2.0 * kPi * (fy + fx);
          const Complex& v = x[at(y, xx)];
          re += (v.re * std::cos(a)) - (v.im * std::sin(a));
          im += (v.re * std::sin(a)) + (v.im * std::cos(a));
        }
      }
      const Complex& got = f[at(ky, kx)];
      CHECK(got.re == doctest::Approx(re).epsilon(1e-9).scale(1.0));
      CHECK(got.im == doctest::Approx(im).epsilon(1e-9).scale(1.0));
    }
  }
  REQUIRE(wg::num::fft_2d(f, kRows, kCols, true).ok());
  CHECK(max_error(f, x) < 1e-12);
}

TEST_CASE("numerics.fft rejects bad sizes") {
  std::vector<Complex> x(6, Complex{0.0, 0.0});
  CHECK(wg::num::fft(x).code == StatusCode::invalid_argument);
  CHECK(wg::num::fft_2d(x, 2, 3).code == StatusCode::invalid_argument);
  CHECK(wg::num::fft_2d(x, 2, 2).code == StatusCode::invalid_argument);
  CHECK(wg::num::fft_2d(x, 0, 2).code == StatusCode::invalid_argument);
  std::vector<Complex> y(8, Complex{0.0, 0.0});
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::fft_2d(y, 2, 4, false, resource).code == StatusCode::out_of_memory);
}
