#include "wg/core/distributions.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include "unit/core/rng_kat.inc"
#include "wg/core/detmath.hpp"
#include "wg/core/rng.hpp"

using wg::core::Expected;
using wg::core::RngKey;
using wg::core::RngStream;
using wg::core::stage_key;
using wg::core::StatusCode;
using wg::core::WeightedBins;

namespace kat = wg::test::kat;

namespace {

// Statistical tests use fixed keys, so they are deterministic (plan section 10). Each runs at
// alpha = 0.01; the same draws were checked with the Python reference tools/rng.
constexpr std::uint64_t kSeed = 20261021;
constexpr std::size_t kSamples = 1000000;
// Bins per axis of the pair-independence test.
constexpr std::size_t kPairBins = 16;

std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }
double from_bits(std::uint64_t b) { return std::bit_cast<double>(b); }

double normal_cdf(double z) { return 0.5 * std::erfc(-z / std::numbers::sqrt2); }

/// Critical Kolmogorov-Smirnov statistic at alpha = 0.01 (asymptotic, large n).
double ks_critical(std::size_t n) { return 1.62762 / std::sqrt(static_cast<double>(n)); }

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

/// Upper 1 % quantile of chi^2 with df degrees of freedom (Wilson-Hilferty approximation).
double chi2_critical(std::size_t df) {
  constexpr double kZ99 = 2.3263478740408408;  // Phi^-1(0.99)
  const auto k = static_cast<double>(df);
  const double c = 1.0 - (2.0 / (9.0 * k)) + (kZ99 * std::sqrt(2.0 / (9.0 * k)));
  return k * c * c * c;
}

/// Pearson chi^2 of observed counts against expected counts.
double chi2_statistic(std::span<const double> expected, std::span<const double> observed) {
  double chi2 = 0.0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const double diff = observed[i] - expected[i];
    chi2 += diff * diff / expected[i];
  }
  return chi2;
}

/// Chi^2 statistic and degrees of freedom of a Poisson sample, merging neighbouring values until
/// each bin expects at least 5 counts.
std::pair<double, std::size_t> poisson_chi2(const std::vector<std::uint64_t>& draws,
                                            double lambda) {
  const auto kmax = static_cast<std::uint64_t>(lambda + (10.0 * std::sqrt(lambda)) + 10.0);
  const auto n = static_cast<double>(draws.size());
  std::vector<double> counts(kmax + 1, 0.0);
  for (const std::uint64_t k : draws) {
    counts[std::min(k, kmax)] += 1.0;
  }
  // log pmf(k) = log pmf(k - 1) + log(lambda) - log(k), starting at -lambda.
  std::vector<double> pmf(kmax + 1, 0.0);
  double mass = 0.0;
  double log_pmf = -lambda;
  for (std::uint64_t k = 0; k <= kmax; ++k) {
    if (k > 0) {
      log_pmf += std::log(lambda) - std::log(static_cast<double>(k));
    }
    pmf[k] = std::exp(log_pmf);
    mass += pmf[k];
  }
  pmf[kmax] += std::max(0.0, 1.0 - mass);  // upper tail

  std::vector<double> expected;
  std::vector<double> observed;
  double e = 0.0;
  double o = 0.0;
  for (std::uint64_t k = 0; k <= kmax; ++k) {
    e += n * pmf[k];
    o += counts[k];
    if (e >= 5.0) {
      expected.push_back(e);
      observed.push_back(o);
      e = 0.0;
      o = 0.0;
    }
  }
  expected.back() += e;
  observed.back() += o;
  return {chi2_statistic(expected, observed), expected.size() - 1};
}

Expected<WeightedBins> make_bins(std::span<const double> weights) {
  return WeightedBins::create(weights);
}

}  // namespace

TEST_CASE("core.distributions known-answer vectors of normal and log-normal") {
  for (const auto& v : kat::kNormal) {
    const RngKey key{v.key};
    const double mean = from_bits(v.mean);
    const double sd = from_bits(v.sd);
    CHECK(bits(wg::core::normal(key, v.cell, v.draw, mean, sd)) == v.normal);
    CHECK(bits(wg::core::log_normal(key, v.cell, v.draw, mean, sd)) == v.log_normal);
  }
  for (const auto& v : kat::kInverseNormalCdf) {
    CAPTURE(from_bits(v.in));
    CHECK(bits(wg::core::inverse_normal_cdf(from_bits(v.in))) == v.out);
  }
}

TEST_CASE("core.distributions known-answer vectors of Poisson draws") {
  for (const auto& v : kat::kPoisson) {
    CAPTURE(from_bits(v.lambda));
    RngStream stream{RngKey{v.key}, v.id};
    for (const std::uint64_t want : v.values) {
      CHECK(wg::core::poisson(stream, from_bits(v.lambda)) == want);
    }
    CHECK(stream.draw() == v.draws);
  }
}

TEST_CASE("core.distributions known-answer vectors of weighted bins") {
  for (const auto& v : kat::kWeightedSample) {
    const auto& set = kat::kWeightSets[v.set];
    const auto bins = make_bins(std::span<const double>(set.weights.data(), set.size));
    REQUIRE(bins);
    CHECK(bins->sample(from_bits(v.u)) == v.bin);
  }
  for (const auto& v : kat::kWeightedKeyed) {
    const auto& set = kat::kWeightSets[v.set];
    const auto bins = make_bins(std::span<const double>(set.weights.data(), set.size));
    REQUIRE(bins);
    CHECK(bins->sample(RngKey{v.key}, v.cell, v.draw) == v.bin);
  }
}

TEST_CASE("core.distributions inverse normal cdf edges and symmetry") {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  CHECK(wg::core::inverse_normal_cdf(0.0) == -kInf);
  CHECK(wg::core::inverse_normal_cdf(-1.0) == -kInf);
  CHECK(wg::core::inverse_normal_cdf(1.0) == kInf);
  CHECK(std::isnan(wg::core::inverse_normal_cdf(std::numeric_limits<double>::quiet_NaN())));
  CHECK(wg::core::inverse_normal_cdf(0.5) == 0.0);
  const double z975 = wg::core::inverse_normal_cdf(0.975);
  CHECK(z975 == doctest::Approx(1.959963984540054).epsilon(1e-15));
  for (const double p : {1e-300, 1e-10, 0.001, 0.07, 0.3}) {
    CAPTURE(p);
    CHECK(normal_cdf(wg::core::inverse_normal_cdf(p)) == doctest::Approx(p).epsilon(1e-12));
  }
  for (const double p : {0.001, 0.07, 0.3}) {
    CAPTURE(p);
    const double lower = wg::core::inverse_normal_cdf(p);
    CHECK(-wg::core::inverse_normal_cdf(1.0 - p) == doctest::Approx(lower).epsilon(1e-10));
  }
}

TEST_CASE("core.distributions stream and stateless samplers agree") {
  const RngKey key = stage_key(kSeed, "test.stream", 0);
  RngStream stream{key, 17};
  for (std::uint64_t k = 0; k < 64; k += 2) {
    const double n = wg::core::normal(stream, 1.0, 2.0);
    const double ln = wg::core::log_normal(stream, 1.0, 2.0);
    REQUIRE(n == wg::core::normal(key, 17, k, 1.0, 2.0));
    REQUIRE(ln == wg::core::log_normal(key, 17, k + 1, 1.0, 2.0));
  }
  for (std::uint64_t i = 0; i < 64; ++i) {
    REQUIRE(wg::core::log_normal(key, i, 0, 0.3, 0.7) ==
            wg::core::det_exp(wg::core::normal(key, i, 0, 0.3, 0.7)));
  }
  CHECK(wg::core::normal(key, 0, 0, 5.0, 0.0) == 5.0);
}

TEST_CASE("core.distributions uniform draws pass KS and pair independence tests") {
  const RngKey key = stage_key(kSeed, "test.uniform", 0);
  std::vector<double> u(kSamples);
  for (std::size_t i = 0; i < kSamples; ++i) {
    u[i] = wg::core::uniform01(key, i, 0);
  }
  CHECK(ks_statistic(u, [](double x) { return x; }) < ks_critical(kSamples));

  // 16 x 16 contingency of (draw 0, draw 1) of one cell and of draw 0 of neighbouring cells.
  constexpr std::size_t kCells = kPairBins * kPairBins;
  std::vector<double> same_cell(kCells, 0.0);
  std::vector<double> next_cell(kCells, 0.0);
  const auto bin = [](double x) {
    return static_cast<std::size_t>(x * static_cast<double>(kPairBins));
  };
  for (std::size_t i = 0; i < kSamples; ++i) {
    const std::size_t row = bin(wg::core::uniform01(key, i, 0)) * kPairBins;
    same_cell[row + bin(wg::core::uniform01(key, i, 1))] += 1.0;
    next_cell[row + bin(wg::core::uniform01(key, i + 1, 0))] += 1.0;
  }
  const double per_cell = static_cast<double>(kSamples) / static_cast<double>(kCells);
  const std::vector<double> expected(kCells, per_cell);
  CHECK(chi2_statistic(expected, same_cell) < chi2_critical(kCells - 1));
  CHECK(chi2_statistic(expected, next_cell) < chi2_critical(kCells - 1));
}

TEST_CASE("core.distributions normal draws pass a KS test at alpha 0.01 over 1e6 samples") {
  // Acceptance test of A04.
  const RngKey key = stage_key(kSeed, "test.normal", 0);
  std::vector<double> z(kSamples);
  for (std::size_t i = 0; i < kSamples; ++i) {
    z[i] = wg::core::normal(key, i, 0, 0.0, 1.0);
  }
  CHECK(ks_statistic(z, normal_cdf) < ks_critical(kSamples));

  // Log-normal draws are det_exp of the same normal draws; their logarithm passes the same test.
  std::vector<double> y(kSamples);
  for (std::size_t i = 0; i < kSamples; ++i) {
    y[i] = (std::log(wg::core::log_normal(key, i, 0, 0.5, 0.75)) - 0.5) / 0.75;
  }
  CHECK(ks_statistic(y, normal_cdf) < ks_critical(kSamples));
}

TEST_CASE("core.distributions weighted bins pass a chi2 test at alpha 0.01 over 1e6 samples") {
  // Acceptance test of A04: five bins, 4 degrees of freedom.
  const RngKey key = stage_key(kSeed, "test.weighted", 0);
  const std::array<double, 5> weights = {1.0, 2.0, 3.0, 4.0, 10.0};
  const auto bins = make_bins(weights);
  REQUIRE(bins);
  CHECK(bins->size() == 5);
  CHECK(bins->total() == 20.0);
  std::array<double, 5> observed{};
  for (std::size_t i = 0; i < kSamples; ++i) {
    observed.at(bins->sample(key, i, 0)) += 1.0;
  }
  std::array<double, 5> expected{};
  for (std::size_t j = 0; j < weights.size(); ++j) {
    expected.at(j) = static_cast<double>(kSamples) * weights.at(j) / bins->total();
  }
  constexpr double kChi2Critical4 = 13.2767;  // chi^2_{4, 0.99}
  CHECK(chi2_statistic(expected, observed) < kChi2Critical4);
}

TEST_CASE("core.distributions Poisson draws follow the Poisson pmf") {
  const RngKey key = stage_key(kSeed, "test.poisson", 0);
  constexpr std::size_t kDraws = 200000;
  const std::array<double, 6> lambdas = {0.5, 4.0, 9.99, 10.0, 30.0, 1000.0};
  for (std::size_t j = 0; j < lambdas.size(); ++j) {
    const double lambda = lambdas.at(j);
    CAPTURE(lambda);
    const RngKey kj = key.derive(j);
    std::vector<std::uint64_t> draws(kDraws);
    for (std::size_t i = 0; i < kDraws; ++i) {
      RngStream stream{kj, i};
      draws[i] = wg::core::poisson(stream, lambda);
    }
    const std::pair<double, std::size_t> chi2 = poisson_chi2(draws, lambda);
    CHECK(chi2.first < chi2_critical(chi2.second));
  }
}

TEST_CASE("core.distributions Poisson mean and variance for large lambda") {
  const RngKey key = stage_key(kSeed, "test.poisson", 0).derive(99);
  constexpr std::size_t kDraws = 200000;
  for (const double lambda : {1e6, 1e12}) {
    CAPTURE(lambda);
    double sum = 0.0;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < kDraws; ++i) {
      RngStream stream{key, i};
      const auto x = static_cast<double>(wg::core::poisson(stream, lambda)) - lambda;
      sum += x;
      sum_sq += x * x;
    }
    const auto n = static_cast<double>(kDraws);
    const double mean_offset = sum / n;
    const double variance = (sum_sq - (sum * mean_offset)) / (n - 1.0);
    CHECK(std::abs(mean_offset) < 4.0 * std::sqrt(lambda / n));
    CHECK(std::abs((variance / lambda) - 1.0) < 0.02);
  }
}

TEST_CASE("core.distributions Poisson edge cases") {
  const RngKey key = stage_key(kSeed, "test.poisson.edges", 0);
  RngStream stream{key, 0};
  CHECK(wg::core::poisson(stream, 0.0) == 0);
  CHECK(stream.draw() == 0);  // lambda = 0 takes no draw
  RngStream a{key, 1};
  RngStream b{key, 1};
  CHECK(wg::core::poisson(a, 1e13) == wg::core::poisson(b, wg::core::kMaxPoissonLambda));
  RngStream tiny{key, 2};
  CHECK(wg::core::poisson(tiny, 1e-300) == 0);
  CHECK(tiny.draw() == 1);
}

TEST_CASE("core.distributions weighted bins validation and edges") {
  CHECK(make_bins({}).error().code == StatusCode::invalid_argument);
  const std::array<double, 2> negative = {1.0, -0.5};
  CHECK(make_bins(negative).error().code == StatusCode::invalid_argument);
  const std::array<double, 2> nan = {1.0, std::numeric_limits<double>::quiet_NaN()};
  CHECK(make_bins(nan).error().code == StatusCode::invalid_argument);
  const std::array<double, 2> inf = {1.0, std::numeric_limits<double>::infinity()};
  CHECK(make_bins(inf).error().code == StatusCode::invalid_argument);
  const std::array<double, 3> zero = {0.0, 0.0, 0.0};
  CHECK(make_bins(zero).error().code == StatusCode::invalid_argument);
  const std::array<double, 2> overflow = {1e308, 1e308};
  CHECK(make_bins(overflow).error().code == StatusCode::invalid_argument);
  const std::vector<double> too_many(wg::core::kMaxWeightedBins + 1, 1.0);
  CHECK(make_bins(too_many).error().code == StatusCode::out_of_range);
  const std::vector<double> most(wg::core::kMaxWeightedBins, 1.0);
  CHECK(make_bins(most).has_value());

  // Zero-weight bins are never chosen, also at the ends of [0, 1).
  const std::array<double, 5> sparse = {0.0, 1.0, 0.0, 2.0, 0.0};
  const auto bins = make_bins(sparse);
  REQUIRE(bins);
  CHECK(bins->sample(0.0) == 1);
  CHECK(bins->sample(1.0 / 3.0) == 3);
  CHECK(bins->sample(1.0 - 0x1p-53) == 3);
  const RngKey key = stage_key(kSeed, "test.weighted.sparse", 0);
  for (std::uint64_t i = 0; i < 10000; ++i) {
    const std::size_t b = bins->sample(key, i, 0);
    REQUIRE((b == 1 || b == 3));
  }
}
