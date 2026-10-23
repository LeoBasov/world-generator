#include "wg/params/validation.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <vector>

#include "wg/params/params.hpp"

namespace {

using wg::params::GenParams;
using wg::params::Severity;
using wg::params::validate;
using wg::params::ValidationIssue;

// The single issue of p, which must have exactly one.
ValidationIssue only_issue(const GenParams& p) {
  const std::vector<ValidationIssue> issues = validate(p);
  REQUIRE(issues.size() == 1);
  return issues[0];
}

bool clean(const GenParams& p) { return validate(p).empty(); }

}  // namespace

TEST_CASE("params.validation the defaults are valid") { CHECK(clean(GenParams{})); }

TEST_CASE("params.validation bounds of parameters set in code") {
  GenParams p;
  p.grid.dx_m = 0.5;
  CHECK(only_issue(p) ==
        ValidationIssue{Severity::error, "$.grid.dx_m", "0.5 is below the minimum 1"});
  p.grid.dx_m = std::numeric_limits<double>::quiet_NaN();
  CHECK(only_issue(p) == ValidationIssue{Severity::error, "$.grid.dx_m", "is not a number"});

  GenParams q;
  q.fields.volcanism.weights[3] = -1.0;
  CHECK(only_issue(q) == ValidationIssue{Severity::error, "$.fields.volcanism.weights[3]",
                                         "-1 is below the minimum 0"});
}

TEST_CASE("params.validation field range min must not exceed max") {
  GenParams p;
  p.fields.rainfall.min = 60.0;
  p.fields.rainfall.max = 60.0;
  CHECK(clean(p));
  p.fields.rainfall.min = 61.0;
  CHECK(only_issue(p) ==
        ValidationIssue{Severity::error, "$.fields.rainfall.min", "min 61 is above max 60"});
}

TEST_CASE("params.validation field ranges stay inside the field registry") {
  GenParams p;
  p.fields.elevation.max = 400.0;
  CHECK(clean(p));
  p.fields.elevation.max = 401.0;  // allowed by the schema bounds, not by the registry
  CHECK(only_issue(p) == ValidationIssue{Severity::error, "$.fields.elevation.max",
                                         "401 is above the maximum 400 of field elevation"});
  GenParams q;
  q.fields.temperature.min = -1000.0;
  CHECK(clean(q));
  q.fields.savagery.min = -1.0;
  CHECK(only_issue(q) == ValidationIssue{Severity::error, "$.fields.savagery.min",
                                         "-1 is below the minimum 0 of field savagery"});
}

TEST_CASE("params.validation every field needs a positive weight") {
  GenParams p;
  p.fields.drainage.weights = {0.0, 0.0, 0.0, 0.0, 0.5};
  CHECK(clean(p));
  p.fields.drainage.weights = {0.0, 0.0, 0.0, 0.0, 0.0};
  CHECK(only_issue(p) == ValidationIssue{Severity::error, "$.fields.drainage.weights",
                                         "at least one weight must be positive"});
}

TEST_CASE("params.validation grid size and refinement") {
  GenParams p;
  for (int n : {17, 33, 65, 129, 257}) {
    p.grid.size = n;
    CHECK(clean(p));
  }
  p.grid.size = 100;
  CHECK(only_issue(p).path == "$.grid.size");
  GenParams q;
  for (int r : {1, 2, 4, 8}) {
    q.grid.refinement = r;
    CHECK(clean(q));
  }
  q.grid.refinement = 3;
  CHECK(only_issue(q) ==
        ValidationIssue{Severity::error, "$.grid.refinement", "3 is not one of 1, 2, 4, 8"});
}

TEST_CASE("params.validation ordered pairs") {
  GenParams p;
  p.elevation_post.plains_z_lo = 200.0;
  p.elevation_post.plains_z_hi = 200.5;
  CHECK(clean(p));
  p.elevation_post.plains_z_hi = 200.0;
  CHECK(only_issue(p).path == "$.elevation_post.plains_z_lo");

  GenParams q;
  q.geology.layer_count_min = 8;
  CHECK(clean(q));
  q.geology.layer_count_min = 9;
  CHECK(only_issue(q).path == "$.geology.layer_count_min");

  GenParams c;
  c.geology.cavern_layer_openness_min[1] = 50;
  c.geology.cavern_layer_openness_max[1] = 50;
  CHECK(clean(c));
  c.geology.cavern_layer_openness_min[1] = 51;
  CHECK(only_issue(c) == ValidationIssue{Severity::error, "$.geology.CAVERN_LAYER_OPENNESS_MIN[1]",
                                         "min 51 is above max 50"});
  GenParams w;
  w.geology.cavern_layer_water_min[2] = 90;
  w.geology.cavern_layer_water_max[2] = 10;
  CHECK(only_issue(w).path == "$.geology.CAVERN_LAYER_WATER_MIN[2]");
}

TEST_CASE("params.validation hypsometry knots") {
  GenParams p;
  p.elevation_post.hypsometry_z = {0.0, 1.0, 2.0, 3.0, 400.0};
  p.elevation_post.hypsometry_cdf = {0.0, 0.0, 0.5, 0.5, 1.0};
  CHECK(clean(p));

  GenParams z = p;
  z.elevation_post.hypsometry_z[2] = 1.0;
  CHECK(only_issue(z).path == "$.elevation_post.hypsometry_z[2]");

  GenParams c = p;
  c.elevation_post.hypsometry_cdf[3] = 0.4;
  CHECK(only_issue(c).path == "$.elevation_post.hypsometry_cdf[3]");

  GenParams ends = p;
  ends.elevation_post.hypsometry_cdf[4] = 0.9;
  CHECK(only_issue(ends).path == "$.elevation_post.hypsometry_cdf");
  ends.elevation_post.hypsometry_cdf = {0.1, 0.2, 0.5, 0.6, 1.0};
  CHECK(only_issue(ends).path == "$.elevation_post.hypsometry_cdf");
}

TEST_CASE("params.validation square counts must fit the grid") {
  GenParams p;
  p.grid.size = 17;  // 289 squares
  p.checks.elevation_ranges = {100, 100, 89};
  CHECK(clean(p));
  p.checks.elevation_ranges = {100, 100, 90};
  CHECK(only_issue(p) ==
        ValidationIssue{Severity::error, "$.checks.ELEVATION_RANGES",
                        "the low, mid and high minimums sum to 290 squares, more than the 289 of "
                        "the grid"});
}

TEST_CASE("params.validation warns about checks the field ranges make impossible") {
  GenParams p;
  p.checks.peak_number_min = 1;
  CHECK(clean(p));  // elevation max 400
  p.fields.elevation.max = 399.5;
  ValidationIssue i = only_issue(p);
  CHECK(i.severity == Severity::warning);
  CHECK(i.path == "$.checks.PEAK_NUMBER_MIN");

  GenParams v;
  v.checks.volcano_min = 2;
  CHECK(clean(v));
  v.fields.volcanism.max = 99.0;
  CHECK(only_issue(v).path == "$.checks.VOLCANO_MIN");

  GenParams r;
  r.checks.river_mins = {0, 5};
  r.fields.elevation.max = 104.0;
  CHECK(clean(r));
  r.fields.elevation.max = 103.0;
  i = only_issue(r);
  CHECK(i.severity == Severity::warning);
  CHECK(i.path == "$.checks.RIVER_MINS");

  GenParams e;
  e.checks.complete_ocean_edge_min = 1;
  e.fields.elevation.min = 99.9;
  CHECK(clean(e));
  e.fields.elevation.min = 100.0;
  CHECK(only_issue(e).path == "$.checks.PARTIAL_OCEAN_EDGE_MIN");
}

TEST_CASE("params.validation has_errors and format_issues") {
  const std::vector<ValidationIssue> warnings = {{Severity::warning, "$.a", "w"}};
  CHECK(!wg::params::has_errors(warnings));
  const std::vector<ValidationIssue> mixed = {{Severity::warning, "$.a", "w"},
                                              {Severity::error, "$.b", "e"}};
  CHECK(wg::params::has_errors(mixed));
  CHECK(wg::params::format_issues(mixed) == "warning: $.a: w\nerror: $.b: e\n");
  CHECK(wg::params::format_issues({}).empty());
}
