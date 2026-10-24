#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::hydro {

/// Parameters of the lake bulges.
struct LakeBulgeParams {
  /// Number of river cells drawn (rivers.lake_bulge_count); >= 0.
  std::int32_t count = 10;
  /// Flood height Delta h_lake [eu] (rivers.lake_bulge_height); finite and >= 0.
  double height = 2.0;
};

/// Bulges lakes out along the rivers (R6.1, plan 7.1; docs/adr/0015-river-network.md).
/// - The candidates are the river nodes that are not mouths and whose cell and 8-neighbours
///   are neither ocean (z < terrain::kSeaLevel) nor lake, in node order.
/// - min(count, candidates) of them are drawn without replacement, by a partial Fisher-Yates
///   shuffle whose k-th draw is core::uniform_index(key, k, 0, remaining).
/// - A drawn cell c floods to the level L, the smallest float32 >= z_c + height (in float64).
///   The lake is c plus every cell that drains to c through the receivers over cells with
///   z < L. Lake cells, and cells next to ocean, another lake or another bulge, are left out
///   and not passed through. A drawn cell already in or next to a bulge is skipped.
/// - Each bulge becomes a core::Lake with spill elevation L and outlet receivers[c].
/// Afterwards all lakes are renumbered 0 ... K-1 in order of their first cell, as label_lakes
/// numbers them, and lake_id is rebuilt from the lakes' cells. Returns the number of bulges.
/// lakes and lake_id are unchanged on error. Errors: invalid_argument for an invalid grid,
/// spans without one value per cell, invalid params, a lake without cells or with a cell out of
/// range, or an inconsistent river graph or one with a cell or downstream node out of range;
/// out_of_memory.
[[nodiscard]] core::Expected<std::int32_t> bulge_lakes(
    std::span<const float> z, std::span<const std::int32_t> receivers,
    const core::RiverGraph& rivers, const core::Grid& grid, const LakeBulgeParams& params,
    core::RngKey key, std::span<std::int32_t> lake_id, std::vector<core::Lake>& lakes) noexcept;

}  // namespace wg::hydro
