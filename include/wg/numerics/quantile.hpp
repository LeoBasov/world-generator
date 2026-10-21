#pragma once

#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::num {

// Quantile mapping (TWI to drainage, A28; hypsometry matching). The values are ranked by
// (value, index), so equal values never depend on the sort implementation. A mask (empty =
// every value) selects the values that take part; outputs of the other values are left as
// they are. NaN values in the mask are refused.

/// Empirical CDF of the mask values: out_i = #{j in mask : v_j <= v_i} / m with m the number of
/// mask values, so out lies in (0, 1] and equal values get equal outputs. Errors:
/// invalid_argument for sizes that differ or a NaN value, out_of_memory for the index buffer.
[[nodiscard]] core::Status empirical_cdf(
    std::span<const float> values, std::span<const std::uint8_t> mask, std::span<float> out,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Quantile p in [0, 1] of ascending sorted values, linearly interpolated between order
/// statistics (sorted[h] with h = p * (m - 1), Hyndman–Fan type 7). Errors: invalid_argument
/// for no values or p outside [0, 1].
[[nodiscard]] core::Expected<double> quantile(std::span<const double> sorted, double p) noexcept;

/// Maps every mask value onto a target distribution: out_i = quantile(target_sorted, F(v_i))
/// with F the empirical CDF above, so the outputs follow the target's distribution and keep the
/// order of the inputs. Errors: those of empirical_cdf, invalid_argument for an empty or
/// unsorted target.
[[nodiscard]] core::Status quantile_map(
    std::span<const float> values, std::span<const std::uint8_t> mask,
    std::span<const double> target_sorted, std::span<float> out,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::num
