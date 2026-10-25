#include "wg/classify/field_ranges.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

using wg::classify::BandCounts;
using wg::classify::RangeBands;
using wg::classify::RangeCheckFailure;
using wg::classify::RangeCounts;
using wg::core::FieldId;

TEST_CASE("classify.field_ranges bands are the thirds of the registry scale") {
  auto elevation = wg::classify::range_bands(FieldId::elevation);
  REQUIRE(elevation);
  CHECK(*elevation ==
        RangeBands{.lo = 0.0, .low_max = 400.0 / 3.0, .high_min = 2.0 * 400.0 / 3.0, .hi = 400.0});
  for (const FieldId id :
       {FieldId::rainfall, FieldId::drainage, FieldId::volcanism, FieldId::savagery}) {
    auto bands = wg::classify::range_bands(id);
    REQUIRE(bands);
    CHECK(*bands == wg::classify::thirds(0.0, 100.0));
    CHECK(bands->low_max == 100.0 / 3.0);
  }
  // region_id has no maximum.
  auto unbounded = wg::classify::range_bands(FieldId::region_id);
  REQUIRE_FALSE(unbounded);
  CHECK(unbounded.error().code == wg::core::StatusCode::invalid_argument);
}

TEST_CASE("classify.field_ranges counts squares per band, a value on a cut in the upper band") {
  const RangeBands b = wg::classify::thirds(0.0, 90.0);  // cuts at exactly 30 and 60
  const std::vector<float> values = {0.0F,  29.99F, 30.0F, 45.0F, 59.99F,
                                     60.0F, 90.0F,  -5.0F, 95.0F, std::nanf("")};
  CHECK(wg::classify::count_bands(values, b) == BandCounts{3, 3, 3});
  CHECK(wg::classify::count_bands({}, b) == BandCounts{0, 0, 0});
}

TEST_CASE("classify.field_ranges reports the first failing entry in field and band order") {
  wg::params::ChecksParams p;
  RangeCounts counts{};
  counts.fill(BandCounts{10, 10, 10});
  CHECK(wg::classify::check_field_ranges(counts, p) == std::nullopt);

  // Just above the count fails, at the count passes.
  p.drainage_ranges = {0, 11, 0};
  CHECK(wg::classify::check_field_ranges(counts, p) ==
        RangeCheckFailure{.check_id = "DRAINAGE_RANGES[1]", .measured = 10.0, .required = 11.0});
  p.drainage_ranges = {0, 10, 0};
  CHECK(wg::classify::check_field_ranges(counts, p) == std::nullopt);

  p.savagery_ranges = {11, 0, 0};
  p.rain_ranges = {0, 0, 12};
  CHECK(wg::classify::check_field_ranges(counts, p) ==
        RangeCheckFailure{.check_id = "RAIN_RANGES[2]", .measured = 10.0, .required = 12.0});
  p.elevation_ranges = {11, 0, 0};
  CHECK(wg::classify::check_field_ranges(counts, p) ==
        RangeCheckFailure{.check_id = "ELEVATION_RANGES[0]", .measured = 10.0, .required = 11.0});
  p = {};
  p.volcanism_ranges = {0, 0, 1};
  counts[3] = BandCounts{5, 5, 0};
  CHECK(wg::classify::check_field_ranges(counts, p) ==
        RangeCheckFailure{.check_id = "VOLCANISM_RANGES[2]", .measured = 0.0, .required = 1.0});
}

TEST_CASE("classify.field_ranges recognises ELEVATION_RANGES check ids") {
  CHECK(wg::classify::is_elevation_ranges("ELEVATION_RANGES[2]"));
  CHECK_FALSE(wg::classify::is_elevation_ranges("RAIN_RANGES[0]"));
  CHECK_FALSE(wg::classify::is_elevation_ranges("PEAK_NUMBER_MIN"));
}
