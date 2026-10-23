#include "wg/terrain/hypsometry.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stats.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/fractal/fractal.hpp"
#include "wg/params/params.hpp"

using wg::core::StatusCode;
using wg::terrain::Hypsometry;

namespace {

constexpr std::array<double, 5> kZ = {1.0, 100.0, 200.0, 300.0, 400.0};
constexpr std::array<double, 5> kCdf = {0.0, 0.3, 0.65, 0.9, 1.0};

Hypsometry curve() { return Hypsometry{.z = kZ, .cdf = kCdf}; }

// Raw elevation of the fractal generator at N = 129: continuous, so values rarely tie.
std::vector<float> fractal_elevation(std::uint64_t seed) {
  const wg::core::Grid grid = wg::test::make_grid(129);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  wg::core::SerialExecutor exec;
  const wg::params::GenParams p;
  REQUIRE(wg::fractal::generate_unclamped_field(
              grid, p.fields.elevation, wg::core::stage_key(seed, "test.hyps", 0), {}, z, exec)
              .ok());
  return z;
}

std::vector<double> to_double(const std::vector<float>& v) { return {v.begin(), v.end()}; }

}  // namespace

TEST_CASE("terrain.hypsometry G and its inverse at and between the knots") {
  const Hypsometry h = curve();
  for (std::size_t k = 0; k < kZ.size(); ++k) {
    CHECK(wg::terrain::hypsometry_cdf(h, kZ[k]) == kCdf[k]);
    CHECK(wg::terrain::hypsometry_quantile(h, kCdf[k]) == kZ[k]);
  }
  CHECK(wg::terrain::hypsometry_cdf(h, 50.5) == doctest::Approx(0.15));
  CHECK(wg::terrain::hypsometry_quantile(h, 0.15) == doctest::Approx(50.5));
  CHECK(wg::terrain::hypsometry_cdf(h, -5.0) == 0.0);
  CHECK(wg::terrain::hypsometry_cdf(h, 500.0) == 1.0);
}

TEST_CASE("terrain.hypsometry the inverse jumps over a flat stretch of G") {
  constexpr std::array<double, 4> z = {0.0, 10.0, 20.0, 30.0};
  constexpr std::array<double, 4> cdf = {0.0, 0.5, 0.5, 1.0};
  const Hypsometry h{.z = z, .cdf = cdf};
  REQUIRE(wg::terrain::validate(h).ok());
  CHECK(wg::terrain::hypsometry_quantile(h, 0.5) == 10.0);
  CHECK(wg::terrain::hypsometry_quantile(h, 0.75) == 25.0);
  CHECK(wg::terrain::hypsometry_quantile(h, std::nextafter(0.5, 1.0)) == doctest::Approx(20.0));
}

TEST_CASE("terrain.hypsometry validate refuses malformed curves") {
  CHECK(wg::terrain::validate(curve()).ok());
  constexpr std::array<double, 1> one = {0.0};
  CHECK(wg::terrain::validate(Hypsometry{.z = one, .cdf = one}).code ==
        StatusCode::invalid_argument);
  constexpr std::array<double, 4> four = {0.0, 0.1, 0.2, 1.0};
  CHECK(wg::terrain::validate(Hypsometry{.z = kZ, .cdf = four}).code ==
        StatusCode::invalid_argument);
  constexpr std::array<double, 5> flat_z = {1.0, 100.0, 100.0, 300.0, 400.0};
  CHECK(wg::terrain::validate(Hypsometry{.z = flat_z, .cdf = kCdf}).code ==
        StatusCode::invalid_argument);
  constexpr std::array<double, 5> falling = {0.0, 0.5, 0.4, 0.9, 1.0};
  CHECK(wg::terrain::validate(Hypsometry{.z = kZ, .cdf = falling}).code ==
        StatusCode::invalid_argument);
  constexpr std::array<double, 5> short_of_one = {0.0, 0.3, 0.65, 0.9, 0.99};
  CHECK(wg::terrain::validate(Hypsometry{.z = kZ, .cdf = short_of_one}).code ==
        StatusCode::invalid_argument);
  constexpr std::array<double, 5> nan_z = {1.0, 100.0, std::numeric_limits<double>::quiet_NaN(),
                                           300.0, 400.0};
  CHECK(wg::terrain::validate(Hypsometry{.z = nan_z, .cdf = kCdf}).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("terrain.hypsometric_remap keeps the rank order and matches G (A13 acceptance)") {
  wg::core::SerialExecutor exec;
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    CAPTURE(seed);
    const std::vector<float> before = fractal_elevation(seed);
    std::vector<float> after = before;
    REQUIRE(wg::terrain::hypsometric_remap(after, curve(), exec).ok());

    const std::vector<double> a = to_double(before);
    const std::vector<double> b = to_double(after);
    CHECK(wg::test::average_ranks(a) == wg::test::average_ranks(b));
    CHECK(wg::test::spearman(a, b) == doctest::Approx(1.0).epsilon(1e-12));

    std::vector<double> samples = b;
    const Hypsometry h = curve();
    const double d = wg::test::ks_statistic(
        samples, [&](double z) { return wg::terrain::hypsometry_cdf(h, z); });
    CHECK(d < 0.02);
  }
}

TEST_CASE("terrain.hypsometric_remap maps equal values to one value and the top to the last knot") {
  wg::core::SerialExecutor exec;
  std::vector<float> z = {400.0F, 5.0F, 400.0F, 5.0F, 7.0F, 400.0F, 2.0F, 400.0F};
  REQUIRE(wg::terrain::hypsometric_remap(z, curve(), exec).ok());
  CHECK(z[0] == 400.0F);
  CHECK(z[2] == 400.0F);
  CHECK(z[5] == 400.0F);
  CHECK(z[7] == 400.0F);
  CHECK(z[1] == z[3]);
  CHECK(z[6] < z[1]);
  CHECK(z[1] < z[4]);
  CHECK(z[4] < z[0]);
  // F(2) = 1/8, so the lowest value lands at G^-1(1/8).
  CHECK(static_cast<double>(z[6]) ==
        doctest::Approx(wg::terrain::hypsometry_quantile(curve(), 0.125)).epsilon(1e-6));
}

TEST_CASE("terrain.hypsometric_remap is independent of the thread count") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  std::vector<float> a = fractal_elevation(7);
  std::vector<float> b = a;
  REQUIRE(wg::terrain::hypsometric_remap(a, curve(), serial).ok());
  REQUIRE(wg::terrain::hypsometric_remap(b, curve(), **pool).ok());
  CHECK(a == b);
}

TEST_CASE("terrain.hypsometric_remap refuses bad input") {
  wg::core::SerialExecutor exec;
  std::vector<float> empty;
  CHECK(wg::terrain::hypsometric_remap(empty, curve(), exec).code == StatusCode::invalid_argument);
  std::vector<float> nan = {1.0F, std::numeric_limits<float>::quiet_NaN()};
  CHECK(wg::terrain::hypsometric_remap(nan, curve(), exec).code == StatusCode::invalid_argument);
  std::vector<float> z = {1.0F, 2.0F};
  constexpr std::array<double, 2> bad = {0.0, 0.5};
  CHECK(wg::terrain::hypsometric_remap(z, Hypsometry{.z = bad, .cdf = bad}, exec).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("terrain.hypsometric_remap returns cancelled when cancel is requested") {
  wg::core::SerialExecutor exec;
  std::vector<float> z = fractal_elevation(1);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::terrain::hypsometric_remap(z, curve(), exec, &cancel).code == StatusCode::cancelled);
}
