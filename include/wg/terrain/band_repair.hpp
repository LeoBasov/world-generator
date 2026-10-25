#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"

namespace wg::terrain {

/// Target of the hypsometry repair: the band cuts of ELEVATION_RANGES (low is z < low_max, high
/// is z >= high_min) and the minimal squares per band.
struct BandTarget {
  /// Fixed scale [lo, hi] of the field; lo < low_max < high_min < hi.
  double lo = 0.0;
  double low_max = 0.0;
  double high_min = 0.0;
  double hi = 0.0;
  /// Minimal squares in the low, mid and high band.
  std::array<std::int32_t, 3> mins{};
};

/// Hypsometry repair of stage C3 (R8, R4.1 "adjusts altitudes when it missed"; ADR 0020). If a
/// band holds fewer squares than its minimum, moves the band boundaries in rank space and remaps
/// z through the monotone piecewise-linear map with knots (a, a), (A, low_max), (B, high_min),
/// (b, b), with a = min(z, lo) and b = max(z, hi):
/// - low count k_l = max(n_low, L), rounded up to a boundary between distinct values; high start
///   p = min(first high rank, m - H), rounded down to such a boundary;
/// - if fewer than M squares lie between them, k_l is lowered towards L, then p raised towards
///   m - H, both to boundaries;
/// - A is low_max if k_l is unchanged, else the smallest value of the new mid band; B likewise.
/// A band whose rank boundaries did not move maps onto itself. The map keeps the rank order and
/// equal values; results are clamped strictly into their band, so the counts afterwards are
/// exactly k_l, p - k_l and m - p. b maps to b, so the peaks at 400 stay. Returns true if the
/// minimums hold (z is unchanged if they held before), false if no boundaries satisfy them
/// (e.g. a plateau larger than the free squares), with z unchanged. Errors: invalid_argument for
/// an empty span, a NaN value or cuts out of order; out_of_memory.
[[nodiscard]] core::Expected<bool> fit_elevation_bands(std::span<float> z,
                                                       const BandTarget& target) noexcept;

}  // namespace wg::terrain
