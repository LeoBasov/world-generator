#pragma once

#include <cstdint>
#include <span>

#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/numerics/neighborhood.hpp"

namespace wg::num {

/// Distance value of a cell that no seed reaches, see bfs_distance.
inline constexpr std::int32_t kUnreached = -1;

/// Multi-source breadth-first distance in steps: 0 on seed cells (seeds_i != 0), otherwise the
/// fewest D4 or D8 steps to a seed through passable cells (passable empty = every cell), or
/// kUnreached. With every cell passable this is the Manhattan (d4) or Chebyshev (d8) distance.
/// Errors: invalid_argument for an invalid grid or a span without one value per cell,
/// out_of_memory for the queue.
[[nodiscard]] core::Status bfs_distance(
    std::span<const std::uint8_t> seeds, std::span<const std::uint8_t> passable,
    const core::Grid& grid, Connectivity connectivity, std::span<std::int32_t> out,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Exact squared Euclidean distance to the nearest seed cell (seeds_i != 0), in cells^2, by the
/// separable lower-envelope algorithm of Felzenszwalb and Huttenlocher (2012). The values are
/// integers, exact in float64 and identical for any executor. +infinity everywhere if there is
/// no seed. Errors: as for bfs_distance, out_of_memory for the per-worker scratch.
[[nodiscard]] core::Status edt_squared(
    std::span<const std::uint8_t> seeds, const core::Grid& grid, std::span<double> out,
    core::Executor& exec,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Exact Euclidean distance to the nearest seed cell in cells: the square root of edt_squared,
/// rounded to float32. Errors: those of edt_squared.
[[nodiscard]] core::Status edt(
    std::span<const std::uint8_t> seeds, const core::Grid& grid, std::span<float> out,
    core::Executor& exec,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::num
