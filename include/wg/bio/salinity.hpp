#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/params/tables.hpp"

namespace wg::bio {

// Coastal salinity of R8.2 (A19): S(d) = 100 e^(-d / lambda) with d the Euclidean distance in
// tiles to the nearest ocean cell, quantised to the classes of data/tables/salinity_classes.json.
// Rivers inherit the class of the cell at their mouth (brackish estuaries). The conventions are
// recorded in docs/adr/0017-vegetation-salinity.md.

/// S(d) = 100 e^(-d / decay_length), from core::det_exp.
[[nodiscard]] double coastal_salinity(double distance, double decay_length) noexcept;

/// Distance beyond which S(d) falls below the lowest non-fresh class:
/// decay_length ln(100 / s_1), with s_1 the min_salinity of the second class (from
/// core::det_log). 0 with fewer than two classes or s_1 >= 100, +infinity for s_1 <= 0.
[[nodiscard]] double fresh_distance(const params::SalinityClasses& classes,
                                    double decay_length) noexcept;

/// Inputs of compute_salinity.
struct SalinityInputs {
  std::span<const std::uint8_t> water{};     ///< hydro::WaterClass per cell (required)
  const core::RiverGraph* rivers = nullptr;  ///< river network of C6; null: no estuaries
};

/// Writes the salinity class of every cell into out. Ocean cells (water ocean) get S = 100,
/// every other cell S(d) with d the exact Euclidean distance to the nearest ocean cell (S = 0
/// without ocean); S is quantised with classes.quantise. A river node whose channel distance
/// to its mouth (the node with downstream -1), summed over its steps of 1 or sqrt(2) tiles, is
/// at most fresh_distance takes the class of the mouth cell if that is higher. out must not
/// overlap the inputs. The result does not depend on the thread count. Errors:
/// invalid_argument for an invalid grid, a span without one value per cell, or a river graph
/// that is inconsistent, has a cell or downstream index out of range, or a cycle;
/// out_of_memory; those of num::edt_squared and core::parallel_for, cancelled included.
[[nodiscard]] core::Status compute_salinity(
    const core::Grid& grid, const SalinityInputs& in, const params::SalinityParams& p,
    const params::SalinityClasses& classes, std::span<std::uint8_t> out, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Sets each lake's salinity_class to the highest class of its cells in salinity. Errors:
/// invalid_argument for an invalid grid, a span without one value per cell, or a lake cell out
/// of range.
[[nodiscard]] core::Status assign_lake_salinity(std::span<const std::uint8_t> salinity,
                                                const core::Grid& grid,
                                                std::span<core::Lake> lakes) noexcept;

}  // namespace wg::bio
