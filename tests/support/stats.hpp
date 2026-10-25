#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

namespace wg::test {

// Statistics of the distribution tests (plan section 10.1). Tests use fixed keys, so they are
// deterministic; every test runs at alpha = 0.01.

/// Critical Kolmogorov-Smirnov statistic at alpha = 0.01 (asymptotic, large n).
inline double ks_critical(std::size_t n) { return 1.62762 / std::sqrt(static_cast<double>(n)); }

/// Kolmogorov-Smirnov statistic D = sup |F_n - F| of the samples (sorted in place).
template <class Cdf>
double ks_statistic(std::vector<double>& samples, Cdf cdf) {
  std::sort(samples.begin(), samples.end());
  const auto n = static_cast<double>(samples.size());
  double d = 0.0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const double f = cdf(samples[i]);
    d = std::max({d, f - (static_cast<double>(i) / n), (static_cast<double>(i + 1) / n) - f});
  }
  return d;
}

/// Ranks 1 ... n of the values; equal values share their average rank.
inline std::vector<double> average_ranks(std::span<const double> v) {
  std::vector<std::size_t> order(v.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
  std::vector<double> ranks(v.size());
  for (std::size_t i = 0; i < order.size();) {
    std::size_t j = i;
    while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]]) {
      ++j;
    }
    const double rank = (static_cast<double>(i + j) / 2.0) + 1.0;
    for (std::size_t k = i; k <= j; ++k) {
      ranks[order[k]] = rank;
    }
    i = j + 1;
  }
  return ranks;
}

/// Spearman rank correlation of a and b (same size >= 2): the Pearson correlation of their
/// average ranks.
inline double spearman(std::span<const double> a, std::span<const double> b) {
  const std::vector<double> ra = average_ranks(a);
  const std::vector<double> rb = average_ranks(b);
  const double mean = (static_cast<double>(ra.size()) + 1.0) / 2.0;
  double sab = 0.0;
  double saa = 0.0;
  double sbb = 0.0;
  for (std::size_t i = 0; i < ra.size(); ++i) {
    sab += (ra[i] - mean) * (rb[i] - mean);
    saa += (ra[i] - mean) * (ra[i] - mean);
    sbb += (rb[i] - mean) * (rb[i] - mean);
  }
  return sab / std::sqrt(saa * sbb);
}

/// Two-sample Kolmogorov-Smirnov statistic D = sup |F_a - F_b| (both sorted in place).
inline double ks_two_sample(std::vector<double>& a, std::vector<double>& b) {
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  const auto na = static_cast<double>(a.size());
  const auto nb = static_cast<double>(b.size());
  std::size_t i = 0;
  std::size_t j = 0;
  double d = 0.0;
  while (i < a.size() && j < b.size()) {
    const double v = std::min(a[i], b[j]);
    while (i < a.size() && a[i] == v) {
      ++i;
    }
    while (j < b.size() && b[j] == v) {
      ++j;
    }
    d = std::max(d, std::abs((static_cast<double>(i) / na) - (static_cast<double>(j) / nb)));
  }
  return d;
}

/// Upper 1 % quantile of chi^2 with df degrees of freedom (Wilson-Hilferty approximation).
inline double chi2_critical(std::size_t df) {
  constexpr double kZ99 = 2.3263478740408408;  // Phi^-1(0.99)
  const auto k = static_cast<double>(df);
  const double c = 1.0 - (2.0 / (9.0 * k)) + (kZ99 * std::sqrt(2.0 / (9.0 * k)));
  return k * c * c * c;
}

/// Pearson chi^2 of observed counts against expected counts.
inline double chi2_statistic(std::span<const double> expected, std::span<const double> observed) {
  double chi2 = 0.0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const double diff = observed[i] - expected[i];
    chi2 += diff * diff / expected[i];
  }
  return chi2;
}

/// Closed interval of counts.
struct CountInterval {
  double lo = 0.0;
  double hi = 0.0;

  [[nodiscard]] bool contains(double x) const { return x >= lo && x <= hi; }
};

/// Central 99 % interval of a Binomial(n, p) count (normal approximation, n p (1 - p) large).
inline CountInterval binomial_interval99(std::size_t n, double p) {
  constexpr double kZ995 = 2.5758293035489004;  // Phi^-1(0.995)
  const double mean = static_cast<double>(n) * p;
  const double sd = std::sqrt(mean * (1.0 - p));
  return {mean - (kZ995 * sd), mean + (kZ995 * sd)};
}

/// Central 99 % interval [lo, hi] of a Poisson(lambda) count: the largest lo and smallest hi
/// with P(N < lo) <= 0.005 and P(N > hi) <= 0.005, from the exact probabilities.
inline CountInterval poisson_interval99(double lambda) {
  const auto pmf = [lambda](double k) {
    return std::exp((k * std::log(lambda)) - lambda - std::lgamma(k + 1.0));
  };
  double cdf = 0.0;
  double k = 0.0;
  CountInterval out;
  bool lo_set = false;
  for (;; k += 1.0) {
    const double p = pmf(k);
    if (!lo_set && cdf + p > 0.005) {
      out.lo = k;
      lo_set = true;
    }
    cdf += p;
    if (cdf >= 0.995) {
      out.hi = k;
      return out;
    }
  }
}

}  // namespace wg::test
