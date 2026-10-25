#include "wg/classify/checks.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace wg::classify {

namespace {

struct CountEntry {
  std::string_view region_type;
  std::array<std::int32_t, 3> params::ChecksRegionCountsParams::*value;
  std::array<std::string_view, 3> check_ids;
};

// In the order of the parameter schema.
constexpr std::array<CountEntry, 9> kEntries = {{
    {"swamp",
     &params::ChecksRegionCountsParams::swamp,
     {"REGION_COUNTS.SWAMP[0]", "REGION_COUNTS.SWAMP[1]", "REGION_COUNTS.SWAMP[2]"}},
    {"desert",
     &params::ChecksRegionCountsParams::desert,
     {"REGION_COUNTS.DESERT[0]", "REGION_COUNTS.DESERT[1]", "REGION_COUNTS.DESERT[2]"}},
    {"forest",
     &params::ChecksRegionCountsParams::forest,
     {"REGION_COUNTS.FOREST[0]", "REGION_COUNTS.FOREST[1]", "REGION_COUNTS.FOREST[2]"}},
    {"mountains",
     &params::ChecksRegionCountsParams::mountains,
     {"REGION_COUNTS.MOUNTAINS[0]", "REGION_COUNTS.MOUNTAINS[1]", "REGION_COUNTS.MOUNTAINS[2]"}},
    {"ocean",
     &params::ChecksRegionCountsParams::ocean,
     {"REGION_COUNTS.OCEAN[0]", "REGION_COUNTS.OCEAN[1]", "REGION_COUNTS.OCEAN[2]"}},
    {"glacier",
     &params::ChecksRegionCountsParams::glacier,
     {"REGION_COUNTS.GLACIER[0]", "REGION_COUNTS.GLACIER[1]", "REGION_COUNTS.GLACIER[2]"}},
    {"tundra",
     &params::ChecksRegionCountsParams::tundra,
     {"REGION_COUNTS.TUNDRA[0]", "REGION_COUNTS.TUNDRA[1]", "REGION_COUNTS.TUNDRA[2]"}},
    {"grassland",
     &params::ChecksRegionCountsParams::grassland,
     {"REGION_COUNTS.GRASSLAND[0]", "REGION_COUNTS.GRASSLAND[1]", "REGION_COUNTS.GRASSLAND[2]"}},
    {"hills",
     &params::ChecksRegionCountsParams::hills,
     {"REGION_COUNTS.HILLS[0]", "REGION_COUNTS.HILLS[1]", "REGION_COUNTS.HILLS[2]"}},
}};

}  // namespace

std::optional<RegionCheckFailure> check_region_counts(std::span<const RegionTypeCount> counts,
                                                      std::span<const std::string> region_types,
                                                      const params::ChecksRegionCountsParams& p,
                                                      RegionCountPhase phase) noexcept {
  for (const CountEntry& e : kEntries) {
    RegionTypeCount count;
    const auto it = std::find(region_types.begin(), region_types.end(), e.region_type);
    const auto type = static_cast<std::size_t>(it - region_types.begin());
    if (it != region_types.end() && type < counts.size()) {
      count = counts[type];
    }
    const std::array<std::int32_t, 3>& required = p.*e.value;
    const auto fails = [&](std::size_t k,
                           std::int64_t measured) -> std::optional<RegionCheckFailure> {
      if (measured < required[k]) {
        return RegionCheckFailure{.check_id = e.check_ids[k],
                                  .measured = static_cast<double>(measured),
                                  .required = static_cast<double>(required[k])};
      }
      return std::nullopt;
    };
    if (phase == RegionCountPhase::initial) {
      if (auto f = fails(0, count.cells)) {
        return f;
      }
      if (auto f = fails(1, count.regions)) {
        return f;
      }
    } else if (auto f = fails(2, count.regions)) {
      return f;
    }
  }
  return std::nullopt;
}

std::optional<RegionCheckFailure> check_subregion_max(std::int64_t regions,
                                                      std::int32_t subregion_max) noexcept {
  if (regions > subregion_max) {
    return RegionCheckFailure{.check_id = "SUBREGION_MAX",
                              .measured = static_cast<double>(regions),
                              .required = static_cast<double>(subregion_max)};
  }
  return std::nullopt;
}

}  // namespace wg::classify
