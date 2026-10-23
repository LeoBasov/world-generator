#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "wg/core/grid.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::test {

// Fixture river graph for stage C7 until C6 writes real ones (A15-A17).

/// River nodes on grid (n >= 9): a main stem along row 4 from column 8 down to its mouth at
/// column 0, and a tributary down column 5 from row 0 joining the stem at (5, 4). Nodes are
/// numbered from the mouth upstream along the stem (0 ... 8), then the tributary from its
/// confluence upwards (9 ... 12).
inline wg::core::RiverGraph river_fixture(const wg::core::Grid& grid) {
  wg::core::RiverGraph g;
  const auto add = [&](std::int32_t x, std::int32_t y, std::int64_t downstream) {
    g.cell.push_back(grid.idx(x, y));
    g.downstream.push_back(downstream);
    g.discharge_m3_yr.push_back(1.0F);
    g.width_class.push_back(1);
    g.order.push_back(1);
    g.name_id.push_back(-1);
  };
  for (std::int32_t x = 0; x <= 8; ++x) {
    add(x, 4, x == 0 ? -1 : x - 1);
  }
  std::int64_t below = 5;  // node of (5, 4)
  for (std::int32_t y = 3; y >= 0; --y) {
    add(5, y, below);
    below = static_cast<std::int64_t>(g.size()) - 1;
  }
  return g;
}

}  // namespace wg::test
