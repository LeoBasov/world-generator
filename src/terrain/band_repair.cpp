#include "wg/terrain/band_repair.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace wg::terrain {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// The largest float32 strictly below t.
float below(double t) noexcept {
  auto f = static_cast<float>(t);
  while (static_cast<double>(f) >= t) {
    f = std::nextafter(f, -std::numeric_limits<float>::infinity());
  }
  return f;
}

// The smallest float32 at or above t.
float at_least(double t) noexcept {
  auto f = static_cast<float>(t);
  while (static_cast<double>(f) < t) {
    f = std::nextafter(f, std::numeric_limits<float>::infinity());
  }
  return f;
}

// Rank boundaries of the sorted values v: k is a boundary if no value is split by it.
class Ranks {
 public:
  explicit Ranks(const std::vector<float>& v) noexcept
      : v_(v), m_(static_cast<std::int64_t>(v.size())) {}

  [[nodiscard]] std::int64_t size() const noexcept { return m_; }
  [[nodiscard]] float at(std::int64_t k) const noexcept { return v_[static_cast<std::size_t>(k)]; }

  // Number of values below c.
  [[nodiscard]] std::int64_t count_below(double c) const noexcept {
    const auto it = std::partition_point(v_.begin(), v_.end(),
                                         [c](float x) { return static_cast<double>(x) < c; });
    return static_cast<std::int64_t>(it - v_.begin());
  }
  [[nodiscard]] bool boundary(std::int64_t k) const noexcept {
    return k == 0 || k == m_ || at(k - 1) < at(k);
  }
  // Smallest boundary >= k (k in [0, m]).
  [[nodiscard]] std::int64_t up(std::int64_t k) const noexcept {
    while (!boundary(k)) {
      ++k;
    }
    return k;
  }
  // Largest boundary <= k (k in [0, m]).
  [[nodiscard]] std::int64_t down(std::int64_t k) const noexcept {
    while (!boundary(k)) {
      --k;
    }
    return k;
  }

 private:
  const std::vector<float>& v_;
  std::int64_t m_;
};

}  // namespace

core::Expected<bool> fit_elevation_bands(std::span<float> z, const BandTarget& target) noexcept {
  if (z.empty()) {
    return core::make_error(core::StatusCode::invalid_argument, "fit_elevation_bands: no values");
  }
  if (!(target.lo < target.low_max && target.low_max < target.high_min &&
        target.high_min < target.hi)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_elevation_bands: need lo < low_max < high_min < hi");
  }
  try {
    std::vector<float> v(z.begin(), z.end());
    if (std::any_of(v.begin(), v.end(), [](float x) { return std::isnan(x); })) {
      return core::make_error(core::StatusCode::invalid_argument, "fit_elevation_bands: NaN value");
    }
    std::sort(v.begin(), v.end());
    const Ranks ranks(v);
    const std::int64_t m = ranks.size();
    const std::int64_t need_low = target.mins[0];
    const std::int64_t need_mid = target.mins[1];
    const std::int64_t need_high = target.mins[2];
    if (need_low < 0 || need_mid < 0 || need_high < 0 || need_low + need_mid + need_high > m) {
      return false;
    }
    // Current boundaries: low is [0, kl0), high is [p0, m).
    const std::int64_t kl0 = ranks.count_below(target.low_max);
    const std::int64_t p0 = ranks.count_below(target.high_min);
    if (kl0 >= need_low && p0 - kl0 >= need_mid && m - p0 >= need_high) {
      return true;
    }

    std::int64_t kl = kl0 >= need_low ? kl0 : ranks.up(need_low);
    std::int64_t p = m - p0 >= need_high ? p0 : ranks.down(m - need_high);
    if (p - kl < need_mid) {
      // Lower the low boundary towards its minimum, then raise the high one.
      const std::int64_t want = p - need_mid;
      if (want < kl) {
        const std::int64_t k = want >= 0 ? ranks.down(want) : -1;
        kl = k >= need_low ? k : ranks.up(need_low);
      }
      if (p - kl < need_mid) {
        const std::int64_t want_p = kl + need_mid;
        if (want_p > m) {
          return false;
        }
        p = ranks.up(want_p);
      }
    }
    if (kl < need_low || p - kl < need_mid || m - p < need_high) {
      return false;
    }

    // Knots (a, a), (A, low_max), (B, high_min), (b, b).
    const double a = std::min(static_cast<double>(ranks.at(0)), target.lo);
    const double b = std::max(static_cast<double>(ranks.at(m - 1)), target.hi);
    const double c1 = kl < m ? static_cast<double>(ranks.at(kl)) : kInf;
    const double c2 = p < m ? static_cast<double>(ranks.at(p)) : kInf;
    const double knot_a = kl == kl0 ? target.low_max : (kl < m ? c1 : b + 1.0);
    const double knot_b = p == p0 ? target.high_min : (p < m ? c2 : b + 1.0);
    const float low_top = below(target.low_max);
    const float mid_bottom = at_least(target.low_max);
    const float mid_top = below(target.high_min);
    const float high_bottom = at_least(target.high_min);
    for (float& cell : z) {
      const auto x = static_cast<double>(cell);
      if (x < c1) {
        const double y = a + ((x - a) * (target.low_max - a) / (knot_a - a));
        cell = std::min(static_cast<float>(y), low_top);
      } else if (x < c2) {
        const double y = target.low_max +
                         ((x - knot_a) * (target.high_min - target.low_max) / (knot_b - knot_a));
        cell = std::clamp(static_cast<float>(y), mid_bottom, mid_top);
      } else if (x == b || !(b > knot_b)) {
        cell = std::max(static_cast<float>(b), high_bottom);
      } else {
        const double y = target.high_min + ((x - knot_b) * (b - target.high_min) / (b - knot_b));
        cell = std::max(static_cast<float>(y), high_bottom);
      }
    }
    return true;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "fit_elevation_bands: out of memory");
  }
}

}  // namespace wg::terrain
