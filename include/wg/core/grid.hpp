#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "wg/core/status.hpp"

namespace wg::core {

/// Index returned for a neighbour outside the grid.
inline constexpr std::int64_t kNoCell = -1;

/// Smallest and largest supported cells per axis. The largest is the simulation grid of the
/// physical profile, N_sim = 8 * (257 - 1) + 1 (plan section 3).
inline constexpr std::int32_t kMinGridN = 3;
inline constexpr std::int32_t kMaxGridN = 2049;

/// D8 directions in a fixed order, clockwise from east. x grows east (column), y grows south
/// (row; row 0 is the north edge). The order is part of the determinism contract (R1): loops
/// over neighbours visit them in this order.
enum class Dir8 : std::uint8_t { e, se, s, sw, w, nw, n, ne };

/// All D8 directions, in order.
inline constexpr std::array<Dir8, 8> kDir8 = {
    Dir8::e, Dir8::se, Dir8::s, Dir8::sw, Dir8::w, Dir8::nw, Dir8::n, Dir8::ne};

/// The four axis directions (D4), in D8 order.
inline constexpr std::array<Dir8, 4> kDir4 = {Dir8::e, Dir8::s, Dir8::w, Dir8::n};

/// Column and row offsets of each direction, indexed by Dir8.
inline constexpr std::array<std::int32_t, 8> kDx = {1, 1, 0, -1, -1, -1, 0, 1};
inline constexpr std::array<std::int32_t, 8> kDy = {0, 1, 1, 1, 0, -1, -1, -1};

constexpr std::int32_t dx(Dir8 d) noexcept { return kDx[static_cast<std::size_t>(d)]; }
constexpr std::int32_t dy(Dir8 d) noexcept { return kDy[static_cast<std::size_t>(d)]; }

/// The direction pointing back, e.g. opposite(Dir8::ne) == Dir8::sw.
constexpr Dir8 opposite(Dir8 d) noexcept {
  return static_cast<Dir8>((static_cast<std::uint8_t>(d) + 4U) % 8U);
}

/// True for the four diagonal directions.
constexpr bool is_diagonal(Dir8 d) noexcept { return (static_cast<std::uint8_t>(d) % 2U) == 1U; }

/// Square raster geometry shared by all fields of a world. Cells are stored row-major:
/// cell index i = y * n + x.
struct Grid {
  std::int32_t n = 0;  ///< cells per axis, n = 2^k + 1
  double dx_m = 0.0;   ///< cell size [m]
  double hu_m = 0.0;   ///< metres per elevation unit (R5.3)

  [[nodiscard]] constexpr std::int64_t idx(std::int32_t x, std::int32_t y) const noexcept {
    return (static_cast<std::int64_t>(y) * n) + x;
  }
  [[nodiscard]] constexpr std::int64_t cells() const noexcept {
    return static_cast<std::int64_t>(n) * n;
  }
  [[nodiscard]] constexpr std::int32_t x_of(std::int64_t i) const noexcept {
    return static_cast<std::int32_t>(i % n);
  }
  [[nodiscard]] constexpr std::int32_t y_of(std::int64_t i) const noexcept {
    return static_cast<std::int32_t>(i / n);
  }
  [[nodiscard]] constexpr bool contains(std::int32_t x, std::int32_t y) const noexcept {
    return x >= 0 && y >= 0 && x < n && y < n;
  }
  [[nodiscard]] constexpr bool contains(std::int64_t i) const noexcept {
    return i >= 0 && i < cells();
  }
  /// True for cells on the outer ring.
  [[nodiscard]] constexpr bool is_border(std::int64_t i) const noexcept {
    const std::int32_t x = x_of(i);
    const std::int32_t y = y_of(i);
    return x == 0 || y == 0 || x == n - 1 || y == n - 1;
  }
  /// Index of the neighbour of cell i in direction d, or kNoCell if it lies outside the grid.
  [[nodiscard]] constexpr std::int64_t neighbor(std::int64_t i, Dir8 d) const noexcept {
    const std::int32_t x = x_of(i) + dx(d);
    const std::int32_t y = y_of(i) + dy(d);
    return contains(x, y) ? idx(x, y) : kNoCell;
  }

  friend constexpr bool operator==(const Grid&, const Grid&) = default;
};

/// Checks that n = 2^k + 1 within [kMinGridN, kMaxGridN] and that dx_m and hu_m are finite
/// and positive.
Status validate(const Grid& grid) noexcept;

}  // namespace wg::core
