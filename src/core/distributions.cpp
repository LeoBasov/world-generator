#include "wg/core/distributions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "wg/core/assert.hpp"
#include "wg/core/detmath.hpp"

// tools/rng/rng_reference.py mirrors this file operation by operation; keep both in step.

namespace wg::core {

namespace {

using Poly7 = std::array<double, 8>;

// Coefficients of AS 241 (PPND16), lowest order first; the denominators B, D, F have a
// leading 1.
constexpr double kA0 = 3.3871328727963666080e0;
constexpr double kA1 = 1.3314166789178437745e+2;
constexpr double kA2 = 1.9715909503065514427e+3;
constexpr double kA3 = 1.3731693765509461125e+4;
constexpr double kA4 = 4.5921953931549871457e+4;
constexpr double kA5 = 6.7265770927008700853e+4;
constexpr double kA6 = 3.3430575583588128105e+4;
constexpr double kA7 = 2.5090809287301226727e+3;
constexpr Poly7 kA = {kA0, kA1, kA2, kA3, kA4, kA5, kA6, kA7};

constexpr double kB0 = 1.0;
constexpr double kB1 = 4.2313330701600911252e+1;
constexpr double kB2 = 6.8718700749205790830e+2;
constexpr double kB3 = 5.3941960214247511077e+3;
constexpr double kB4 = 2.1213794301586595867e+4;
constexpr double kB5 = 3.9307895800092710610e+4;
constexpr double kB6 = 2.8729085735721942674e+4;
constexpr double kB7 = 5.2264952788528545610e+3;
constexpr Poly7 kB = {kB0, kB1, kB2, kB3, kB4, kB5, kB6, kB7};

constexpr double kC0 = 1.42343711074968357734e0;
constexpr double kC1 = 4.63033784615654529590e0;
constexpr double kC2 = 5.76949722146069140550e0;
constexpr double kC3 = 3.64784832476320460504e0;
constexpr double kC4 = 1.27045825245236838258e0;
constexpr double kC5 = 2.41780725177450611770e-1;
constexpr double kC6 = 2.27238449892691845833e-2;
constexpr double kC7 = 7.74545014278341407640e-4;
constexpr Poly7 kC = {kC0, kC1, kC2, kC3, kC4, kC5, kC6, kC7};

constexpr double kD0 = 1.0;
constexpr double kD1 = 2.05319162663775882187e0;
constexpr double kD2 = 1.67638483018380384940e0;
constexpr double kD3 = 6.89767334985100004550e-1;
constexpr double kD4 = 1.48103976427480074590e-1;
constexpr double kD5 = 1.51986665636164571966e-2;
constexpr double kD6 = 5.47593808499534494600e-4;
constexpr double kD7 = 1.05075007164441684324e-9;
constexpr Poly7 kD = {kD0, kD1, kD2, kD3, kD4, kD5, kD6, kD7};

constexpr double kE0 = 6.65790464350110377720e0;
constexpr double kE1 = 5.46378491116411436990e0;
constexpr double kE2 = 1.78482653991729133580e0;
constexpr double kE3 = 2.96560571828504891230e-1;
constexpr double kE4 = 2.65321895265761230930e-2;
constexpr double kE5 = 1.24266094738807843860e-3;
constexpr double kE6 = 2.71155556874348757815e-5;
constexpr double kE7 = 2.01033439929228813265e-7;
constexpr Poly7 kE = {kE0, kE1, kE2, kE3, kE4, kE5, kE6, kE7};

constexpr double kF0 = 1.0;
constexpr double kF1 = 5.99832206555887937690e-1;
constexpr double kF2 = 1.36929880922735805310e-1;
constexpr double kF3 = 1.48753612908506148525e-2;
constexpr double kF4 = 7.86869131145613259100e-4;
constexpr double kF5 = 1.84631831751005468180e-5;
constexpr double kF6 = 1.42151175831644588870e-7;
constexpr double kF7 = 2.04426310338993978564e-15;
constexpr Poly7 kF = {kF0, kF1, kF2, kF3, kF4, kF5, kF6, kF7};

/// Horner evaluation c[7] r^7 + ... + c[0], highest coefficient first.
double poly7(const Poly7& c, double r) noexcept {
  double v = c[7];
  for (std::size_t i = 7; i > 0; --i) {
    v = (v * r) + c[i - 1];
  }
  return v;
}

// Poisson: means below this use inversion, from it on PTRS.
constexpr double kPoissonPtrsMin = 10.0;
// PTRS candidates above this are rejected before conversion to an integer.
constexpr double kPoissonMaxK = 0x1p62;

std::uint64_t poisson_inversion(RngStream& stream, double lambda) noexcept {
  const double u = stream.uniform01();
  double p = det_exp(-lambda);
  double cdf = p;
  std::uint64_t k = 0;
  while (u >= cdf && p > 0.0) {
    ++k;
    p = p * lambda / static_cast<double>(k);
    cdf += p;
  }
  return k;
}

std::uint64_t poisson_ptrs(RngStream& stream, double lambda) noexcept {
  const double slam = std::sqrt(lambda);
  const double loglam = det_log(lambda);
  const double b = 0.931 + (2.53 * slam);
  const double a = -0.059 + (0.02483 * b);
  const double log_invalpha = det_log(1.1239 + (1.1328 / (b - 3.4)));
  const double vr = 0.9277 - (3.6224 / (b - 2.0));
  for (;;) {
    const double u = stream.uniform_open01() - 0.5;
    const double v = stream.uniform_open01();
    const double us = 0.5 - std::abs(u);
    const double kd = std::floor((((2.0 * a / us) + b) * u) + lambda + 0.43);
    if (kd < 0.0 || kd > kPoissonMaxK) {
      continue;
    }
    if (us >= 0.07 && v <= vr) {
      return static_cast<std::uint64_t>(kd);
    }
    if (us < 0.013 && v > us) {
      continue;
    }
    const auto k = static_cast<std::uint64_t>(kd);
    const double lhs = det_log(v) + log_invalpha - det_log((a / (us * us)) + b);
    const double rhs = -lambda + (kd * loglam) - log_factorial(k);
    if (lhs <= rhs) {
      return k;
    }
  }
}

}  // namespace

double inverse_normal_cdf(double p) noexcept {
  if (std::isnan(p)) {
    return p;
  }
  if (p <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }
  if (p >= 1.0) {
    return std::numeric_limits<double>::infinity();
  }
  const double q = p - 0.5;
  if (std::abs(q) <= 0.425) {
    const double r = 0.180625 - (q * q);
    return q * poly7(kA, r) / poly7(kB, r);
  }
  double r = q < 0.0 ? p : 1.0 - p;
  r = std::sqrt(-det_log(r));
  double val = 0.0;
  if (r <= 5.0) {
    r -= 1.6;
    val = poly7(kC, r) / poly7(kD, r);
  } else {
    r -= 5.0;
    val = poly7(kE, r) / poly7(kF, r);
  }
  return q < 0.0 ? -val : val;
}

double normal(RngKey key, std::uint64_t cell, std::uint64_t draw, double mean, double sd) noexcept {
  WG_ASSERT(sd >= 0.0, "normal: sd must be non-negative");
  return mean + (sd * inverse_normal_cdf(uniform_open01(key, cell, draw)));
}

double normal(RngStream& stream, double mean, double sd) noexcept {
  WG_ASSERT(sd >= 0.0, "normal: sd must be non-negative");
  return mean + (sd * inverse_normal_cdf(stream.uniform_open01()));
}

double log_normal(RngKey key, std::uint64_t i, std::uint64_t k, double mu, double sigma) noexcept {
  return det_exp(normal(key, i, k, mu, sigma));
}

double log_normal(RngStream& stream, double mu, double sigma) noexcept {
  return det_exp(normal(stream, mu, sigma));
}

std::uint64_t poisson(RngStream& stream, double lambda) noexcept {
  WG_ASSERT(!std::isnan(lambda) && lambda >= 0.0, "poisson: lambda must be non-negative");
  if (!(lambda > 0.0)) {
    return 0;
  }
  lambda = std::min(lambda, kMaxPoissonLambda);
  if (lambda < kPoissonPtrsMin) {
    return poisson_inversion(stream, lambda);
  }
  return poisson_ptrs(stream, lambda);
}

Expected<WeightedBins> WeightedBins::create(std::span<const double> weights) noexcept {
  if (weights.empty()) {
    return make_error(StatusCode::invalid_argument, "weighted bins: no weights");
  }
  if (weights.size() > kMaxWeightedBins) {
    return make_error(StatusCode::out_of_range,
                      "weighted bins: more than " + std::to_string(kMaxWeightedBins) + " bins");
  }
  WeightedBins bins;
  double acc = 0.0;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    const double w = weights[i];
    if (!std::isfinite(w) || w < 0.0) {
      const std::string message = "weighted bins: weight " + std::to_string(i);
      return make_error(StatusCode::invalid_argument, message + " is negative or not finite");
    }
    acc += w;
    bins.cdf_[i] = acc;
    if (w > 0.0) {
      bins.last_positive_ = i;
    }
  }
  if (!std::isfinite(acc) || !(acc > 0.0)) {
    return make_error(StatusCode::invalid_argument,
                      "weighted bins: the weights must have a finite positive sum");
  }
  bins.size_ = weights.size();
  return bins;
}

std::size_t WeightedBins::sample(double u) const noexcept {
  const double target = u * total();
  const double* const first = cdf_.data();
  const double* const last = first + size_;
  const auto i = static_cast<std::size_t>(std::upper_bound(first, last, target) - first);
  return std::min(i, last_positive_);
}

}  // namespace wg::core
