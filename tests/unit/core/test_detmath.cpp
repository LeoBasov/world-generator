#include "wg/core/detmath.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "unit/core/rng_kat.inc"
#include "wg/core/rng.hpp"

using wg::core::det_exp;
using wg::core::det_log;

namespace kat = wg::test::kat;

namespace {

std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }
double from_bits(std::uint64_t b) { return std::bit_cast<double>(b); }

/// Units in the last place between two finite doubles of the same sign.
std::uint64_t ulp_distance(double a, double b) {
  const std::uint64_t x = bits(a);
  const std::uint64_t y = bits(b);
  return x > y ? x - y : y - x;
}

constexpr double kInf = std::numeric_limits<double>::infinity();

/// ln(k!) from the platform libm. The tests are single-threaded, so lgamma's global signgam
/// does not matter.
double reference_log_factorial(std::uint64_t k) {
  return std::lgamma(static_cast<double>(k) + 1.0);  // NOLINT(concurrency-mt-unsafe)
}

}  // namespace

TEST_CASE("core.detmath known-answer vectors are identical on every platform") {
  for (const auto& v : kat::kDetExp) {
    CAPTURE(from_bits(v.in));
    CHECK(bits(det_exp(from_bits(v.in))) == v.out);
  }
  for (const auto& v : kat::kDetLog) {
    CAPTURE(from_bits(v.in));
    CHECK(bits(det_log(from_bits(v.in))) == v.out);
  }
  for (const auto& v : kat::kLogFactorial) {
    CAPTURE(v.k);
    CHECK(bits(wg::core::log_factorial(v.k)) == v.out);
  }
}

TEST_CASE("core.detmath exp and log agree with the platform libm within 2 ulp") {
  // det_* and std:: are each within 1 ulp of the exact result.
  const wg::core::RngKey key = wg::core::stage_key(0, "test.detmath", 0);
  std::uint64_t worst_exp = 0;
  std::uint64_t worst_log = 0;
  for (std::uint64_t i = 0; i < 100000; ++i) {
    const double x = wg::core::uniform(key, i, 0, -700.0, 709.0);
    worst_exp = std::max(worst_exp, ulp_distance(det_exp(x), std::exp(x)));
    const double y = std::exp(wg::core::uniform(key, i, 1, -740.0, 709.0));
    worst_log = std::max(worst_log, ulp_distance(det_log(y), std::log(y)));
  }
  CHECK(worst_exp <= 2);
  CHECK(worst_log <= 2);
}

TEST_CASE("core.detmath special values") {
  CHECK(det_exp(0.0) == 1.0);
  CHECK(det_exp(-0.0) == 1.0);
  CHECK(det_exp(1.0) == std::bit_cast<double>(0x4005BF0A8B145769ULL));  // e, correctly rounded
  CHECK(det_exp(kInf) == kInf);
  CHECK(det_exp(-kInf) == 0.0);
  CHECK(det_exp(710.0) == kInf);
  CHECK(det_exp(-746.0) == 0.0);
  CHECK(det_exp(-740.0) > 0.0);  // subnormal result
  CHECK(std::isnan(det_exp(std::numeric_limits<double>::quiet_NaN())));

  CHECK(det_log(1.0) == 0.0);
  CHECK(det_log(0.0) == -kInf);
  CHECK(det_log(-0.0) == -kInf);
  CHECK(det_log(kInf) == kInf);
  CHECK(std::isnan(det_log(-1.0)));
  CHECK(std::isnan(det_log(-kInf)));
  CHECK(std::isnan(det_log(std::numeric_limits<double>::quiet_NaN())));
  CHECK(det_log(std::numeric_limits<double>::denorm_min()) < -744.0);
}

TEST_CASE("core.detmath log_factorial matches lgamma") {
  for (std::uint64_t k = 0; k < 40; ++k) {
    CAPTURE(k);
    const double want = reference_log_factorial(k);
    CHECK(std::abs(wg::core::log_factorial(k) - want) <= 1e-13 * std::max(1.0, want));
  }
  for (const std::uint64_t k : {100ULL, 1000ULL, 1000000ULL, 1000000000000ULL}) {
    CAPTURE(k);
    const double want = reference_log_factorial(k);
    CHECK(std::abs(wg::core::log_factorial(k) - want) <= 1e-13 * want);
  }
  CHECK(wg::core::log_factorial(0) == 0.0);
  CHECK(wg::core::log_factorial(1) == 0.0);
}
