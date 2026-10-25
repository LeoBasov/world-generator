#include "wg/classify/checks.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using wg::classify::check_region_counts;
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
    ChecksRegionCountsParams p;
    p.*members[m] = {40, 4, 6};
    // [0]: cells.
    auto f = check_region_counts(counts(39, 10), kTypes, p, RegionCountPhase::initial);
    REQUIRE(f);
    CHECK(f->check_id == prefix + "[0]");
    CHECK(f->measured == 39.0);
    CHECK(f->required == 40.0);
    // [1]: initial regions.
    f = check_region_counts(counts(40, 3), kTypes, p, RegionCountPhase::initial);
    REQUIRE(f);
    CHECK(f->check_id == prefix + "[1]");
    CHECK_FALSE(check_region_counts(counts(40, 4), kTypes, p, RegionCountPhase::initial));
    // [2]: final regions, which ignore [0] and [1].
    f = check_region_counts(counts(0, 5), kTypes, p, RegionCountPhase::final);
    REQUIRE(f);
    CHECK(f->check_id == prefix + "[2]");
    CHECK(f->measured == 5.0);
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
  CHECK(check_region_counts(c, kTypes, p, RegionCountPhase::initial)->check_id ==
        "REGION_COUNTS.HILLS[1]");
  // The first failure in parameter order: swamp before hills.
  p.swamp = {1, 0, 0};
  CHECK(check_region_counts(c, kTypes, p, RegionCountPhase::initial)->check_id ==
        "REGION_COUNTS.SWAMP[0]");
  // A region type missing from the table counts 0.
  const std::vector<std::string> without_hills = {"swamp"};
  p.swamp = {0, 0, 0};
  const auto missing =
      check_region_counts(counts(9, 9), without_hills, p, RegionCountPhase::initial);
  REQUIRE(missing);
  CHECK(missing->check_id == "REGION_COUNTS.HILLS[1]");
  CHECK(missing->measured == 0.0);
}

TEST_CASE("classify.check_subregion_max fails just above its maximum") {
  CHECK_FALSE(wg::classify::check_subregion_max(2750, 2750));
  const auto f = wg::classify::check_subregion_max(2751, 2750);
  REQUIRE(f);
  CHECK(f->check_id == "SUBREGION_MAX");
  CHECK(f->measured == 2751.0);
  CHECK(f->required == 2750.0);
}
