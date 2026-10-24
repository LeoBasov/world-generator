#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/status.hpp"

namespace wg::hydro {

// Test-river incision of R6.1 and R6.3 (A16). The conventions are recorded in
// docs/adr/0014-test-rivers-thermal-erosion.md.

/// Spacing q = 2^-15 eu of the elevation lattice. Every multiple of q with magnitude below
/// kLatticeLimit is a float32, so digging and thermal transfers on lattice values are exact.
inline constexpr double kErosionQuantum = 1.0 / 32768.0;

/// Values on the lattice must lie in (-kLatticeLimit, kLatticeLimit).
inline constexpr double kLatticeLimit = 512.0;

/// The multiple of kErosionQuantum nearest to v (ties to even). v must be finite with
/// |v| < kLatticeLimit.
[[nodiscard]] double snap_to_lattice(double v) noexcept;

/// True if v is a multiple of kErosionQuantum with |v| < kLatticeLimit.
[[nodiscard]] bool on_lattice(double v) noexcept;

/// Moves every value of z to the nearest lattice point, by at most q / 2. Errors:
/// invalid_argument (and z unchanged) for a value that is not finite or not below
/// kLatticeLimit in magnitude.
[[nodiscard]] core::Status snap_to_lattice(std::span<float> z) noexcept;

/// Sources of test rivers (plan 7.1): the mountain-edge cells, z >= terrain::kMountainElevation
/// with at least one 8-neighbour below it, in ascending index order. Errors: invalid_argument
/// for an invalid grid or a span without one value per cell, out_of_memory.
[[nodiscard]] core::Expected<std::vector<std::int32_t>> find_river_sources(
    std::span<const float> z, const core::Grid& grid) noexcept;

/// Parameters of one walker.
struct WalkerParams {
  /// Depth epsilon [eu] dug at the saddle of a depression (erosion.dig_depth); finite and >= 0.
  double dig_depth = 1.0;
  /// Step limit L_max (erosion.walker_step_limit); >= 0.
  std::int32_t step_limit = 1024;
};

/// Why a walker stopped.
enum class WalkerStop : std::uint8_t {
  ocean,       ///< it reached a cell with z < terrain::kSeaLevel
  mask,        ///< it reached a cell of the stop mask (a lake)
  step_limit,  ///< it took step_limit steps
  stuck,       ///< no unvisited cell was left to move to or to flood
};

/// End of a walk.
struct WalkerResult {
  std::int32_t end = -1;   ///< cell the walker stopped on
  std::int32_t steps = 0;  ///< moves plus flooded cells
  WalkerStop stop = WalkerStop::ocean;
};

/// One test river (R6.3, ADR 0014). The walker never returns to a cell it visited. At cell i
/// it moves to its lowest unvisited 8-neighbour if that is lower than z_i, choosing among equal
/// lowest neighbours uniformly with one draw of stream, which keys the walk to its source (plan
/// 6.3). Otherwise i is a pit or a flat: the walker floods the depression from i, taking
/// unvisited cells in (z, index) order with the water level the highest z taken so far, until
/// it takes a cell below the level, which lies beyond the saddle. It digs the highest cell of
/// the flood route from i to that cell, the saddle, down to max(z - dig_depth,
/// terrain::kSeaLevel), so it never creates ocean, and continues from the cell beyond. Each
/// walker therefore lowers the outlet of every depression it passes by dig_depth. The walker
/// stops on ocean, on a cell of stop_mask (empty: none), after step_limit steps (a move or a
/// flooded cell is one step), or when stuck (no unvisited cell left to move to or flood).
/// visited is scratch with one byte per cell that must be 0 on entry and is 0 again on return.
/// path, if given, receives the walk from the source to the end, flood routes included, as a
/// chain of 8-neighbours. Errors: invalid_argument for an invalid grid, spans without one value
/// per cell, a source outside the grid or invalid params; out_of_memory.
[[nodiscard]] core::Expected<WalkerResult> run_walker(
    std::span<float> z, const core::Grid& grid, std::int32_t source, const WalkerParams& params,
    core::RngStream& stream, std::span<std::uint8_t> visited,
    std::span<const std::uint8_t> stop_mask = {},
    std::vector<std::int32_t>* path = nullptr) noexcept;

}  // namespace wg::hydro
