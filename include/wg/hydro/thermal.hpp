#pragma once

#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"

namespace wg::hydro {

/// Parameters of the thermal step.
struct ThermalParams {
  /// Talus threshold Theta [eu] (erosion.talus_threshold); finite and >= 0.
  double talus_threshold = 30.0;
  /// Fraction c of the excess moved per transfer (erosion.talus_fraction); in [0, 1].
  double fraction = 0.5;
  /// Transfers after which the step gives up with internal (a safety net; the step always
  /// ends); >= 1.
  std::int64_t max_transfers = std::int64_t{1} << 40;
};

/// Cliff (thermal) erosion of R6.4, run to completion. Where the difference Delta between two
/// 8-neighbours exceeds Theta, the higher cell gives d to the lower one: c / 2 (Delta - Theta)
/// rounded up to the lattice of hydro::kErosionQuantum, at least one quantum and at most half
/// of Delta. The cells are relaxed from a FIFO work list, which starts with every cell that has
/// a steep pair in row-major order; a popped cell checks its neighbours in core::kDir8 order,
/// and every cell whose value changes is queued again. The step ends when the list is empty,
/// so every neighbour difference ends <= Theta (Theta below one quantum counts as one
/// quantum). The transfers are exact, so the sum of z is unchanged, and each moves both cells
/// towards their mean. fraction 0 changes nothing. Returns the number of transfers. Errors:
/// invalid_argument for an invalid grid, a span without one value per cell, a value off the
/// lattice (see snap_to_lattice) or invalid params; internal after max_transfers transfers
/// (z is unchanged then); out_of_memory.
[[nodiscard]] core::Expected<std::int64_t> thermal_erode(
    std::span<float> z, const core::Grid& grid, const ThermalParams& params,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::hydro
