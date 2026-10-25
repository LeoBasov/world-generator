#include "wg/classify/checks.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using wg::classify::check_region_counts;
using wg::classify::RegionCheckFailure;
using wg::classify::RegionCountPhase;
using wg::classify::RegionTypeCount;
using wg::params::ChecksRegionCountsParams;

namespace {

const std::vector<std::string> kTypes = {"mountains", "glacier",   "tundra", "swamp",
                                         "forest",    "grassland", "hills",  "desert",
                                         "ocean",     "lake",      "river"};

std::vector<RegionTypeCount> counts(std::int64_t cells, std::int64_t regions) {
  return std::vector<RegionTypeCount>(kTypes.size(), {.cells = cells, .regions = regions});
}

// The expected failure of a check.
std::optional<RegionCheckFailure> failure(std::string_view id, double measured, double required) {
  return RegionCheckFailure{.check_id = id, .measured = measured, .required = required};
}

}  // namespace

TEST_CASE("classify.check_region_counts passes at and fails just below every threshold") {
  const auto members =
      std::array{&ChecksRegionCountsParams::swamp,  &ChecksRegionCountsParams::desert,
                 &ChecksRegionCountsParams::forest, &ChecksRegionCountsParams::mountains,
                 &ChecksRegionCountsParams::ocean,  &ChecksRegionCountsParams::glacier,
                 &ChecksRegionCountsParams::tundra, &ChecksRegionCountsParams::grassland,
                 &ChecksRegionCountsParams::hills};
  const auto names = std::array{"SWAMP",   "DESERT", "FOREST",    "MOUNTAINS", "OCEAN",
                                "GLACIER", "TUNDRA", "GRASSLAND", "HILLS"};
  CHECK_FALSE(check_region_counts(counts(0, 0), kTypes, {}, RegionCountPhase::initial));
  CHECK_FALSE(check_region_counts(counts(0, 0), kTypes, {}, RegionCountPhase::final));
  for (std::size_t m = 0; m < members.size(); ++m) {
    CAPTURE(names[m]);
    const std::string prefix = std::string("REGION_COUNTS.") + names[m];
    const std::string cells_id = prefix + "[0]";
    const std::string initial_id = prefix + "[1]";
    const std::string final_id = prefix + "[2]";
    ChecksRegionCountsParams p;
    p.*members[m] = {40, 4, 6};
    // [0]: cells.
    CHECK(check_region_counts(counts(39, 10), kTypes, p, RegionCountPhase::initial) ==
          failure(cells_id, 39.0, 40.0));
    // [1]: initial regions.
    CHECK(check_region_counts(counts(40, 3), kTypes, p, RegionCountPhase::initial) ==
          failure(initial_id, 3.0, 4.0));
    CHECK_FALSE(check_region_counts(counts(40, 4), kTypes, p, RegionCountPhase::initial));
    // [2]: final regions, which ignore [0] and [1].
    CHECK(check_region_counts(counts(0, 5), kTypes, p, RegionCountPhase::final) ==
          failure(final_id, 5.0, 6.0));
    CHECK_FALSE(check_region_counts(counts(0, 6), kTypes, p, RegionCountPhase::final));
  }
}

TEST_CASE("classify.check_region_counts reads each count of its own region type") {
  ChecksRegionCountsParams p;
  p.hills = {0, 2, 0};
  std::vector<RegionTypeCount> c = counts(0, 0);
  c[6].regions = 2;  // hills
  CHECK_FALSE(check_region_counts(c, kTypes, p, RegionCountPhase::initial));
  c[6].regions = 1;
  CHECK(check_region_counts(c, kTypes, p, RegionCountPhase::initial) ==
        failure("REGION_COUNTS.HILLS[1]", 1.0, 2.0));
  // The first failure in parameter order: swamp before hills.
  p.swamp = {1, 0, 0};
  CHECK(check_region_counts(c, kTypes, p, RegionCountPhase::initial) ==
        failure("REGION_COUNTS.SWAMP[0]", 0.0, 1.0));
  // A region type missing from the table counts 0.
  const std::vector<std::string> without_hills = {"swamp"};
  p.swamp = {0, 0, 0};
  CHECK(check_region_counts(counts(9, 9), without_hills, p, RegionCountPhase::initial) ==
        failure("REGION_COUNTS.HILLS[1]", 0.0, 2.0));
}

TEST_CASE("classify.check_subregion_max fails just above its maximum") {
  CHECK_FALSE(wg::classify::check_subregion_max(2750, 2750));
  CHECK(wg::classify::check_subregion_max(2751, 2750) == failure("SUBREGION_MAX", 2751.0, 2750.0));
}
