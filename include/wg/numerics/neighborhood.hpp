#pragma once

#include <cstdint>
#include <numbers>
#include <span>

#include "wg/core/grid.hpp"

namespace wg::num {

/// Which neighbours count as adjacent: the four axis neighbours or all eight.
enum class Connectivity : std::uint8_t { d4, d8 };

/// sqrt(2), the length of a diagonal step in cells.
inline constexpr double kSqrt2 = std::numbers::sqrt2;

/// Directions of a connectivity in D8 order (core::kDir4 or core::kDir8).
[[nodiscard]] constexpr std::span<const core::Dir8> directions(Connectivity c) noexcept {
  if (c == Connectivity::d4) {
    return core::kDir4;
  }
  return core::kDir8;
}

/// Distance to the neighbour in direction d, in cells: 1 or sqrt(2).
[[nodiscard]] constexpr double step_length(core::Dir8 d) noexcept {
  return core::is_diagonal(d) ? kSqrt2 : 1.0;
}

/// Calls f(j, d) for every neighbour j of cell i that lies inside the grid, in D8 order.
template <class F>
constexpr void for_each_neighbor(const core::Grid& grid, std::int64_t i, Connectivity c,
                                 F&& f) noexcept {
  for (const core::Dir8 d : directions(c)) {
    const std::int64_t j = grid.neighbor(i, d);
    if (j != core::kNoCell) {
      f(j, d);
    }
  }
}

}  // namespace wg::num
