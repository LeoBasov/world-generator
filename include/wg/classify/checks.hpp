#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "wg/classify/regions.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::classify {

// The region checks of R11 (A20): REGION_COUNTS after the first vegetation pass (C4) and in
// the final verification (C11), SUBREGION_MAX in the final verification.

/// Which entries of REGION_COUNTS a check reads: initial uses [0] (cells, the report's
/// "squares") and [1] (regions), final uses [2] (regions).
enum class RegionCountPhase : std::uint8_t { initial, final };

/// A failed check: its id (e.g. "REGION_COUNTS.SWAMP[1]"), the measured and the required value.
struct RegionCheckFailure {
  std::string_view check_id;
  double measured = 0.0;
  double required = 0.0;

  friend bool operator==(const RegionCheckFailure&, const RegionCheckFailure&) = default;
};

/// The first REGION_COUNTS entry of phase whose count is below its parameter, in parameter order
/// (swamp, desert, forest, mountains, ocean, glacier, tundra, grassland, hills) and, in the
/// initial phase, [0] before [1]; nullopt if all pass. counts is indexed like region_types; a
/// parameter whose region type is not in region_types counts 0.
[[nodiscard]] std::optional<RegionCheckFailure> check_region_counts(
    std::span<const RegionTypeCount> counts, std::span<const std::string> region_types,
    const params::ChecksRegionCountsParams& p, RegionCountPhase phase) noexcept;

/// SUBREGION_MAX: fails if there are more than subregion_max regions.
[[nodiscard]] std::optional<RegionCheckFailure> check_subregion_max(
    std::int64_t regions, std::int32_t subregion_max) noexcept;

}  // namespace wg::classify
