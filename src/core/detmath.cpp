// det_exp and det_log are ports of FreeBSD msun e_exp.c and e_log.c, which carry this notice:
//
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this
//   software is freely granted, provided that this notice
//   is preserved.
//
// The algorithms and constants are unchanged; the code is restructured for C++20 (std::bit_cast
// instead of word-access macros, special cases returned as constants instead of raised through
// arithmetic). tools/rng/rng_reference.py mirrors it operation by operation.

#include "wg/core/detmath.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace wg::core {

namespace {

constexpr double from_bits(std::uint64_t b) noexcept { return std::bit_cast<double>(b); }

constexpr std::uint32_t high_word(double x) noexcept {
  return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x) >> 32U);
}

constexpr std::uint32_t low_word(double x) noexcept {
  return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x) & 0xFFFFFFFFULL);
}

constexpr double with_high_word(double x, std::uint32_t hi) noexcept {
  return from_bits((static_cast<std::uint64_t>(hi) << 32U) | low_word(x));
}

// Constants given as bit patterns, exactly as in fdlibm.
constexpr double kLn2Hi = from_bits(0x3FE62E42FEE00000ULL);
constexpr double kLn2Lo = from_bits(0x3DEA39EF35793C76ULL);
constexpr double kInvLn2 = from_bits(0x3FF71547652B82FEULL);
constexpr double kEulerE = from_bits(0x4005BF0A8B145769ULL);
constexpr double kTwo54 = from_bits(0x4350000000000000ULL);
constexpr double kTwoM1000 = from_bits(0x0170000000000000ULL);
constexpr double kTwo1023 = from_bits(0x7FE0000000000000ULL);
constexpr double kOverflowThreshold = from_bits(0x40862E42FEFA39EFULL);
constexpr double kUnderflowThreshold = from_bits(0xC0874910D52D3051ULL);

constexpr double kExpP1 = from_bits(0x3FC555555555553EULL);
constexpr double kExpP2 = from_bits(0xBF66C16C16BEBD93ULL);
constexpr double kExpP3 = from_bits(0x3F11566AAF25DE2CULL);
constexpr double kExpP4 = from_bits(0xBEBBBD41C5D26BF1ULL);
constexpr double kExpP5 = from_bits(0x3E66376972BEA4D0ULL);

constexpr double kLg1 = from_bits(0x3FE5555555555593ULL);
constexpr double kLg2 = from_bits(0x3FD999999997FA04ULL);
constexpr double kLg3 = from_bits(0x3FD2492494229359ULL);
constexpr double kLg4 = from_bits(0x3FCC71C51D8E78AFULL);
constexpr double kLg5 = from_bits(0x3FC7466496CB03DEULL);
constexpr double kLg6 = from_bits(0x3FC39A09D078C69FULL);
constexpr double kLg7 = from_bits(0x3FC2F112DF3E5244ULL);

constexpr double kHalfLog2Pi = 0.91893853320467274178;  // ln(sqrt(2 pi))

// Below this k, k! is exact in a double and ln(k!) comes from det_log directly.
constexpr std::uint64_t kExactFactorials = 10;

}  // namespace

double det_exp(double x) noexcept {
  std::uint32_t hx = high_word(x);
  const bool negative = (hx >> 31U) != 0;
  hx &= 0x7FFFFFFFU;

  if (hx >= 0x40862E42U) {  // |x| >= 709.78...
    if (hx >= 0x7FF00000U) {
      if (std::isnan(x)) {
        return x + x;
      }
      return negative ? 0.0 : x;  // exp(-inf) = 0, exp(+inf) = +inf
    }
    if (x > kOverflowThreshold) {
      return std::numeric_limits<double>::infinity();
    }
    if (x < kUnderflowThreshold) {
      return 0.0;
    }
  }

  // Argument reduction: x = k ln2 + r with |r| <= 0.5 ln2, r = hi - lo.
  double hi = 0.0;
  double lo = 0.0;
  std::int32_t k = 0;
  if (hx > 0x3FD62E42U) {    // |x| > 0.5 ln2
    if (hx < 0x3FF0A2B2U) {  // and |x| < 1.5 ln2
      if (x == 1.0) {
        return kEulerE;
      }
      if (negative) {
        hi = x + kLn2Hi;
        lo = -kLn2Lo;
        k = -1;
      } else {
        hi = x - kLn2Hi;
        lo = kLn2Lo;
        k = 1;
      }
    } else {
      k = static_cast<std::int32_t>((kInvLn2 * x) + (negative ? -0.5 : 0.5));
      const auto t = static_cast<double>(k);
      hi = x - (t * kLn2Hi);  // exact
      lo = t * kLn2Lo;
    }
    x = hi - lo;
  } else if (hx < 0x3E300000U) {  // |x| < 2^-28
    return 1.0 + x;
  }

  // x is now in the primary range.
  const double t = x * x;
  const std::int32_t scale = k >= -1021 ? k : k + 1000;
  const double twopk = from_bits(static_cast<std::uint64_t>(0x3FF + scale) << 52U);
  // Horner steps of x - t (P1 + t (P2 + t (P3 + t (P4 + t P5)))), in fdlibm's order.
  double p = kExpP4 + (t * kExpP5);
  p = kExpP3 + (t * p);
  p = kExpP2 + (t * p);
  p = kExpP1 + (t * p);
  const double c = x - (t * p);
  if (k == 0) {
    return 1.0 - (((x * c) / (c - 2.0)) - x);
  }
  const double y = 1.0 - ((lo - ((x * c) / (2.0 - c))) - hi);
  if (k >= -1021) {
    if (k == 1024) {
      return y * 2.0 * kTwo1023;
    }
    return y * twopk;
  }
  return y * twopk * kTwoM1000;
}

double det_log(double x) noexcept {
  auto hx = static_cast<std::int32_t>(high_word(x));
  const std::uint32_t lx = low_word(x);

  std::int32_t k = 0;
  if (hx < 0x00100000) {  // x < 2^-1022
    if (((static_cast<std::uint32_t>(hx) & 0x7FFFFFFFU) | lx) == 0) {
      return -std::numeric_limits<double>::infinity();  // log(+-0)
    }
    if (hx < 0) {
      return std::numeric_limits<double>::quiet_NaN();  // log(negative)
    }
    k -= 54;
    x *= kTwo54;  // subnormal: scale up
    hx = static_cast<std::int32_t>(high_word(x));
  }
  if (hx >= 0x7FF00000) {
    return x + x;  // +inf or NaN
  }
  k += (hx >> 20) - 1023;
  hx &= 0x000FFFFF;
  const std::int32_t i0 = (hx + 0x95F64) & 0x100000;
  x = with_high_word(x, static_cast<std::uint32_t>(hx | (i0 ^ 0x3FF00000)));  // x or x/2
  k += i0 >> 20;
  const double f = x - 1.0;
  if ((0x000FFFFF & (2 + hx)) < 3) {  // -2^-20 <= f < 2^-20
    if (f == 0.0) {
      if (k == 0) {
        return 0.0;
      }
      const auto dk = static_cast<double>(k);
      return (dk * kLn2Hi) + (dk * kLn2Lo);
    }
    const double r = f * f * (0.5 - (0.33333333333333333 * f));
    if (k == 0) {
      return f - r;
    }
    const auto dk = static_cast<double>(k);
    return (dk * kLn2Hi) - ((r - (dk * kLn2Lo)) - f);
  }
  const double s = f / (2.0 + f);
  const auto dk = static_cast<double>(k);
  const double z = s * s;
  std::int32_t i = hx - 0x6147A;
  const double w = z * z;
  const std::int32_t j = 0x6B851 - hx;
  const double t1 = w * (kLg2 + (w * (kLg4 + (w * kLg6))));
  const double t2 = z * (kLg1 + (w * (kLg3 + (w * (kLg5 + (w * kLg7))))));
  i |= j;
  const double r = t2 + t1;
  if (i > 0) {
    const double hfsq = 0.5 * f * f;
    if (k == 0) {
      return f - (hfsq - (s * (hfsq + r)));
    }
    return (dk * kLn2Hi) - ((hfsq - ((s * (hfsq + r)) + (dk * kLn2Lo))) - f);
  }
  if (k == 0) {
    return f - (s * (f - r));
  }
  return (dk * kLn2Hi) - (((s * (f - r)) - (dk * kLn2Lo)) - f);
}

double log_factorial(std::uint64_t k) noexcept {
  if (k < kExactFactorials) {
    double factorial = 1.0;
    for (std::uint64_t i = 2; i <= k; ++i) {
      factorial *= static_cast<double>(i);
    }
    return det_log(factorial);
  }
  const auto x = static_cast<double>(k + 1);
  const double z = 1.0 / x;
  const double z2 = z * z;
  // z (1/12 - z^2 (1/360 - z^2 (1/1260 - z^2 (1/1680 - z^2 / 1188)))), innermost first.
  double series = (1.0 / 1680.0) - (z2 * (1.0 / 1188.0));
  series = (1.0 / 1260.0) - (z2 * series);
  series = (1.0 / 360.0) - (z2 * series);
  series = z * ((1.0 / 12.0) - (z2 * series));
  return ((x - 0.5) * det_log(x)) - x + kHalfLog2Pi + series;
}

}  // namespace wg::core
