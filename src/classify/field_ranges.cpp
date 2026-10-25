#include "wg/classify/field_ranges.hpp"

#include <cstddef>
#include <string>

namespace wg::classify {

namespace {

struct RangeEntry {
  std::array<std::int32_t, 3> params::ChecksParams::*value;
  std::array<std::string_view, 3> check_ids;
};

// Indexed like kRangeFields.
constexpr std::array<RangeEntry, 5> kEntries = {{
    {&params::ChecksParams::elevation_ranges,
     {"ELEVATION_RANGES[0]", "ELEVATION_RANGES[1]", "ELEVATION_RANGES[2]"}},
    {&params::ChecksParams::rain_ranges, {"RAIN_RANGES[0]", "RAIN_RANGES[1]", "RAIN_RANGES[2]"}},
    {&params::ChecksParams::drainage_ranges,
     {"DRAINAGE_RANGES[0]", "DRAINAGE_RANGES[1]", "DRAINAGE_RANGES[2]"}},
    {&params::ChecksParams::volcanism_ranges,
     {"VOLCANISM_RANGES[0]", "VOLCANISM_RANGES[1]", "VOLCANISM_RANGES[2]"}},
    {&params::ChecksParams::savagery_ranges,
     {"SAVAGERY_RANGES[0]", "SAVAGERY_RANGES[1]", "SAVAGERY_RANGES[2]"}},
}};

}  // namespace

RangeBands thirds(double lo, double hi) noexcept {
  const double span = hi - lo;
  return {.lo = lo, .low_max = lo + (span / 3.0), .high_min = lo + (2.0 * span / 3.0), .hi = hi};
}

core::Expected<RangeBands> range_bands(core::FieldId id) noexcept {
  const core::FieldInfo& info = core::field_info(id);
  if (!info.range.has_min || !info.range.has_max) {
    try {
      return core::make_error(core::StatusCode::invalid_argument,
                              "field " + std::string(info.name) + " has no bounded range");
    } catch (...) {
      return core::make_error(core::StatusCode::invalid_argument, "field has no bounded range");
    }
  }
  return thirds(info.range.min, info.range.max);
}

BandCounts count_bands(std::span<const float> values, const RangeBands& bands) noexcept {
  BandCounts counts{};
  for (const float f : values) {
    const auto v = static_cast<double>(f);
    if (v < bands.low_max) {
      ++counts[0];
    } else if (v >= bands.high_min) {
      ++counts[2];
    } else if (v >= bands.low_max) {
      ++counts[1];
    }
  }
  return counts;
}

std::optional<RangeCheckFailure> check_field_ranges(const RangeCounts& counts,
                                                    const params::ChecksParams& p) noexcept {
  for (std::size_t f = 0; f < kEntries.size(); ++f) {
    const std::array<std::int32_t, 3>& required = p.*kEntries[f].value;
    for (std::size_t k = 0; k < 3; ++k) {
      if (counts[f][k] < required[k]) {
        return RangeCheckFailure{.check_id = kEntries[f].check_ids[k],
                                 .measured = static_cast<double>(counts[f][k]),
                                 .required = static_cast<double>(required[k])};
      }
    }
  }
  return std::nullopt;
}

bool is_elevation_ranges(std::string_view check_id) noexcept {
  return check_id.starts_with("ELEVATION_RANGES");
}

}  // namespace wg::classify
