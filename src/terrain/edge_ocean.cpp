#include "wg/terrain/edge_ocean.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::terrain {

namespace {

namespace detail = num::detail;

// Elevation of a lowered cell on the edge or at the centre of a bay: just below sea level.
constexpr double kShore = static_cast<double>(kSeaLevel) - 1.0;

// Cell t (0 ... n-1) along an edge.
std::int64_t edge_cell(const core::Grid& grid, Edge edge, std::int32_t t) noexcept {
  const std::int32_t last = grid.n - 1;
  switch (edge) {
    case Edge::north:
      return grid.idx(t, 0);
    case Edge::east:
      return grid.idx(last, t);
    case Edge::south:
      return grid.idx(t, last);
    case Edge::west:
      break;
  }
  return grid.idx(0, t);
}

// Distance in cells from cell i to the edge line.
double edge_distance(const core::Grid& grid, Edge edge, std::int64_t i) noexcept {
  const std::int32_t last = grid.n - 1;
  switch (edge) {
    case Edge::north:
      return grid.y_of(i);
    case Edge::east:
      return last - grid.x_of(i);
    case Edge::south:
      return last - grid.y_of(i);
    case Edge::west:
      break;
  }
  return grid.x_of(i);
}

// Lowers every cell to cap(d) where d = distance(i) < width.
template <class Distance>
void lower(std::span<float> z, const core::Grid& grid, double width, const Distance& distance) {
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const double d = distance(i);
    if (d < width) {
      const double cap = kShore + ((d / width) * (static_cast<double>(kMaxElevation) - kShore));
      float& v = z[static_cast<std::size_t>(i)];
      v = std::min(v, static_cast<float>(cap));
    }
  }
}

EdgeOceans count(std::span<const float> z, const core::Grid& grid) noexcept {
  EdgeOceans result;
  for (const Edge edge : kEdges) {
    std::int32_t cells = 0;
    for (std::int32_t t = 0; t < grid.n; ++t) {
      if (z[static_cast<std::size_t>(edge_cell(grid, edge, t))] < kSeaLevel) {
        ++cells;
      }
    }
    result.ocean_cells[static_cast<std::size_t>(edge)] = cells;
    result.partial += cells > 0 ? 1 : 0;
    result.complete += cells == grid.n ? 1 : 0;
  }
  return result;
}

core::Status check_input(std::span<const float> z, const core::Grid& grid,
                         std::string_view message) noexcept {
  return detail::first_error({core::validate(grid), detail::check_field(z, grid, message)});
}

}  // namespace

core::Expected<EdgeOceans> count_ocean_edges(std::span<const float> z,
                                             const core::Grid& grid) noexcept {
  if (core::Status s = check_input(z, grid, "count_ocean_edges: z must have one value per cell");
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  return count(z, grid);
}

core::Status enforce_edge_oceans(std::span<float> z, const core::Grid& grid,
                                 std::int32_t complete_min, std::int32_t partial_min,
                                 double width) noexcept {
  if (core::Status s = check_input(z, grid, "enforce_edge_oceans: z must have one value per cell");
      !s.ok()) {
    return s;
  }
  if (complete_min < 0 || complete_min > 4 || partial_min < 0 || partial_min > 4 ||
      !std::isfinite(width) || width < 1.0) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "enforce_edge_oceans: minima must lie in [0, 4], width >= 1");
  }

  // Complete edges: the edges with the most ocean first; stable keeps Edge order on ties.
  EdgeOceans now = count(z, grid);
  std::array<Edge, 4> order = kEdges;
  std::stable_sort(order.begin(), order.end(), [&](Edge a, Edge b) {
    return now.ocean_cells[static_cast<std::size_t>(a)] >
           now.ocean_cells[static_cast<std::size_t>(b)];
  });
  for (const Edge edge : order) {
    if (now.complete >= complete_min) {
      break;
    }
    if (now.ocean_cells[static_cast<std::size_t>(edge)] == grid.n) {
      continue;
    }
    lower(z, grid, width, [&](std::int64_t i) { return edge_distance(grid, edge, i); });
    now = count(z, grid);
  }

  // Partial edges: a bay at the lowest cell of the edges without ocean, lowest minimum first.
  std::array<std::int64_t, 4> lowest{};
  for (const Edge edge : kEdges) {
    std::int64_t best = edge_cell(grid, edge, 0);
    for (std::int32_t t = 1; t < grid.n; ++t) {
      const std::int64_t c = edge_cell(grid, edge, t);
      const float vc = z[static_cast<std::size_t>(c)];
      const float vb = z[static_cast<std::size_t>(best)];
      if (vc < vb || (vc == vb && c < best)) {
        best = c;
      }
    }
    lowest[static_cast<std::size_t>(edge)] = best;
  }
  order = kEdges;
  std::stable_sort(order.begin(), order.end(), [&](Edge a, Edge b) {
    return z[static_cast<std::size_t>(lowest[static_cast<std::size_t>(a)])] <
           z[static_cast<std::size_t>(lowest[static_cast<std::size_t>(b)])];
  });
  for (const Edge edge : order) {
    if (now.partial >= partial_min) {
      break;
    }
    if (now.ocean_cells[static_cast<std::size_t>(edge)] > 0) {
      continue;
    }
    const std::int64_t c = lowest[static_cast<std::size_t>(edge)];
    const double cx = grid.x_of(c);
    const double cy = grid.y_of(c);
    // sqrt is correctly rounded (IEEE 754); std::hypot is not, so it could differ by platform.
    lower(z, grid, width, [&](std::int64_t i) {
      const double ddx = grid.x_of(i) - cx;
      const double ddy = grid.y_of(i) - cy;
      return std::sqrt((ddx * ddx) + (ddy * ddy));
    });
    now = count(z, grid);
  }
  return {};
}

}  // namespace wg::terrain
