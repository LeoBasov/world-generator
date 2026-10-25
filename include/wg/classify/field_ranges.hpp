#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::classify {

// The field-statistics checks of R11 (A22): ELEVATION_RANGES, RAIN_RANGES, DRAINAGE_RANGES,
// VOLCANISM_RANGES and SAVAGERY_RANGES are minimal counts of low, mid and high squares. The
// bands are the thirds of the field's fixed registry scale (ADR 0020).

/// Band cuts of a field: low is [lo, low_max), mid [low_max, high_min), high [high_min, hi].
struct RangeBands {
  double lo = 0.0;
  double low_max = 0.0;
  double high_min = 0.0;
  double hi = 0.0;

  friend bool operator==(const RangeBands&, const RangeBands&) = default;
};

/// Squares per band: low, mid, high.
using BandCounts = std::array<std::int64_t, 3>;

/// The thirds of [lo, hi], computed in float64: low_max = lo + (hi - lo) / 3, high_min =
/// lo + 2 (hi - lo) / 3.
[[nodiscard]] RangeBands thirds(double lo, double hi) noexcept;

/// The thirds of the registry range of field id (elevation 0-400 cuts at 133.33 and 266.67, the
/// 0-100 fields at 33.33 and 66.67). Errors: invalid_argument for a field without a minimum and
/// a maximum.
[[nodiscard]] core::Expected<RangeBands> range_bands(core::FieldId id) noexcept;

/// Counts the values per band. A value on a cut belongs to the upper band; values below lo count
/// as low and above hi as high, NaN in no band.
[[nodiscard]] BandCounts count_bands(std::span<const float> values,
                                     const RangeBands& bands) noexcept;

/// The fields of the *_RANGES checks, in check order.
inline constexpr std::array<core::FieldId, 5> kRangeFields = {
    core::FieldId::elevation, core::FieldId::rainfall, core::FieldId::drainage,
    core::FieldId::volcanism, core::FieldId::savagery};

/// Band counts of the kRangeFields, in the same order.
using RangeCounts = std::array<BandCounts, 5>;

/// A failed check: its id (e.g. "RAIN_RANGES[2]"), the measured count and the minimum.
struct RangeCheckFailure {
  std::string_view check_id;
  double measured = 0.0;
  double required = 0.0;

  friend bool operator==(const RangeCheckFailure&, const RangeCheckFailure&) = default;
};

/// The first *_RANGES entry whose count is below its minimum, in field order and then low, mid,
/// high; nullopt if all pass.
[[nodiscard]] std::optional<RangeCheckFailure> check_field_ranges(
    const RangeCounts& counts, const params::ChecksParams& p) noexcept;

/// True if check_id names an entry of ELEVATION_RANGES.
[[nodiscard]] bool is_elevation_ranges(std::string_view check_id) noexcept;

}  // namespace wg::classify
