#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
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

}  // namespace wg::test
