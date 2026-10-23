#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"

namespace wg::terrain {

/// The four map edges in their fixed order (row 0 is the north edge, column 0 the west edge).
enum class Edge : std::uint8_t { north, east, south, west };

inline constexpr std::array<Edge, 4> kEdges = {Edge::north, Edge::east, Edge::south, Edge::west};

/// Ocean on the map edges (R11: PARTIAL_OCEAN_EDGE_MIN, COMPLETE_OCEAN_EDGE_MIN). An edge is
/// partly ocean if at least one of its cells lies below sea level and completely ocean if all
/// do; a complete edge also counts as partial.
struct EdgeOceans {
  /// Ocean cells per edge, indexed by Edge.
  std::array<std::int32_t, 4> ocean_cells{};
  std::int32_t partial = 0;
  std::int32_t complete = 0;
};

/// Counts the ocean cells (z < kSeaLevel) on each edge. Errors: invalid_argument for an invalid
/// grid or a span without one value per cell.
[[nodiscard]] core::Expected<EdgeOceans> count_ocean_edges(std::span<const float> z,
                                                           const core::Grid& grid) noexcept;

/// Edge-ocean repair of stage C3 (ADR 0011). Lowers elevation until at least complete_min edges
/// are completely and partial_min edges partly ocean; an edge that already qualifies is left as
/// it is. Lowering caps a cell at cap(d) = s + (d / width) (400 - s) for d < width, with s just
/// below sea level and d the distance in cells:
/// - complete: d is the distance to the edge line. The edges taken are those with the most
///   ocean cells, ties in Edge order.
/// - partial (after the complete step): d is the distance to the edge's lowest cell (ties to the
///   lower index), which opens a bay. The edges taken are those with the lowest minimum, ties
///   in Edge order.
/// Errors: invalid_argument for an invalid grid, a span without one value per cell, minima
/// outside [0, 4] or a width that is not finite and >= 1.
[[nodiscard]] core::Status enforce_edge_oceans(std::span<float> z, const core::Grid& grid,
                                               std::int32_t complete_min, std::int32_t partial_min,
                                               double width) noexcept;

}  // namespace wg::terrain
