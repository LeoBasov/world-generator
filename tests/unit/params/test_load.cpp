#include "wg/params/load.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "io/json.hpp"
#include "support/source_files.hpp"

namespace {

using wg::core::StatusCode;
using wg::params::GenParams;
using wg::params::load_params;
using wg::params::read_params;
using wg::params::Severity;
using wg::params::ValidationIssue;

std::vector<ValidationIssue> issues_of(std::string_view json) {
  const auto result = read_params(json);
  REQUIRE(result.has_value());
  return result->issues;
}

// The single error of a parameter text; load_params must refuse it with that line.
ValidationIssue only_error(std::string_view json) {
  const std::vector<ValidationIssue> issues = issues_of(json);
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].severity == Severity::error);
  const auto loaded = load_params(json);
  REQUIRE(!loaded.has_value());
  CHECK(loaded.error().code == StatusCode::invalid_argument);
  CHECK(loaded.error().message.find(issues[0].path + ": " + issues[0].message) !=
        std::string::npos);
  return issues[0];
}

}  // namespace

TEST_CASE("params.load a document with only the version gives the defaults") {
  const auto doc = load_params(R"({"schema_version": 1})");
  REQUIRE(doc.has_value());
  CHECK(doc->params == GenParams{});
  CHECK(!doc->seed.has_value());
  CHECK(!doc->profile.has_value());
}

TEST_CASE("params.load values override the defaults") {
  const auto doc = load_params(R"({
    "schema_version": 1, "seed": 42, "profile": "classic",
    "grid": {"size": 65, "dx_m": 250},
    "poles": "both",
    "fields": {"elevation": {"weights": [5, 1, 1, 1, 1], "mesh": 2}},
    "erosion": {"PERIODICALLY_ERODE_EXTREMES": false},
    "checks": {"RIVER_MINS": [3, 4], "REGION_COUNTS": {"HILLS": [1, 2, 3]}}
  })");
  REQUIRE(doc.has_value());
  CHECK(doc->seed == std::uint64_t{42});
  CHECK(doc->profile == std::string("classic"));
  const GenParams& p = doc->params;
  CHECK(p.grid.size == 65);
  CHECK(p.grid.dx_m == 250.0);
  CHECK(p.grid.hu_m == GenParams{}.grid.hu_m);
  CHECK(p.poles == wg::params::Poles::both);
  CHECK(p.fields.elevation.weights[0] == 5.0);
  CHECK(p.fields.elevation.mesh == 2);
  CHECK(p.fields.elevation.max == 400.0);  // default of this field, not of the shared struct
  CHECK(!p.erosion.periodically_erode_extremes);
  CHECK(p.checks.river_mins[1] == 4);
  CHECK(p.checks.region_counts.hills[2] == 3);
}

TEST_CASE("params.load out-of-range values are refused with path and message") {
  CHECK(only_error(R"({"schema_version": 1, "grid": {"dx_m": 0}})") ==
        ValidationIssue{Severity::error, "$.grid.dx_m", "0 is below the minimum 1"});
  CHECK(only_error(R"({"schema_version": 1, "grid": {"size": 513}})") ==
        ValidationIssue{Severity::error, "$.grid.size", "513 is above the maximum 257"});
  CHECK(only_error(
            R"({"schema_version": 1, "fields": {"elevation": {"weights": [1, 1, -2, 1, 1]}}})") ==
        ValidationIssue{Severity::error, "$.fields.elevation.weights[2]",
                        "-2 is below the minimum 0"});
  CHECK(only_error(R"({"schema_version": 1, "poles": "west"})") ==
        ValidationIssue{Severity::error, "$.poles",
                        "\"west\" is not one of none, north, south, both"});
}

TEST_CASE("params.load min above max is refused with path and message") {
  CHECK(only_error(R"({"schema_version": 1, "fields": {"rainfall": {"min": 80, "max": 20}}})") ==
        ValidationIssue{Severity::error, "$.fields.rainfall.min", "min 80 is above max 20"});
  CHECK(only_error(R"({"schema_version": 1, "fields": {"elevation": {"max": 500}}})") ==
        ValidationIssue{Severity::error, "$.fields.elevation.max",
                        "500 is above the maximum 400 of field elevation"});
}

TEST_CASE("params.load wrong types and shapes") {
  CHECK(only_error(R"({"schema_version": 1, "grid": {"size": "big"}})").message ==
        "expected integer, got string");
  CHECK(only_error(R"({"schema_version": 1, "grid": {"size": 129.0}})").message ==
        "expected integer, got number");
  CHECK(only_error(R"({"schema_version": 1, "grid": 5})") ==
        ValidationIssue{Severity::error, "$.grid", "expected object, got integer"});
  CHECK(only_error(R"({"schema_version": 1, "erosion": {"PERIODICALLY_ERODE_EXTREMES": 1}})")
            .message == "expected boolean, got integer");
  CHECK(only_error(R"({"schema_version": 1, "fields": {"elevation": {"weights": [1, 1]}}})")
            .message == "expected 5 elements, got 2");
  CHECK(only_error(R"({"schema_version": 1, "fields": {"elevation": {"weights": 3}}})").message ==
        "expected array of 5 numbers, got integer");
  CHECK(only_error(R"({"schema_version": 1, "poles": 1})").message ==
        "expected string, got integer");
}

TEST_CASE("params.load unknown keys are refused") {
  CHECK(only_error(R"({"schema_version": 1, "erosion": {"FOO": 1}})") ==
        ValidationIssue{Severity::error, "$.erosion.FOO", "unknown parameter"});
  CHECK(only_error(R"({"schema_version": 1, "foo": {}})").path == "$.foo");
  // A dotted key does not address a nested parameter.
  CHECK(only_error(R"({"schema_version": 1, "grid.size": 65})").message == "unknown parameter");
}

TEST_CASE("params.load document keys") {
  CHECK(only_error(R"({"grid": {}})") ==
        ValidationIssue{Severity::error, "$.schema_version", "missing required key"});
  CHECK(only_error(R"({"schema_version": 0})").path == "$.schema_version");
  CHECK(only_error(R"({"schema_version": "1"})").message == "expected integer, got string");
  CHECK(only_error(R"({"schema_version": 1, "seed": -1})") ==
        ValidationIssue{Severity::error, "$.seed", "-1 is below the minimum 0"});
  CHECK(only_error(R"({"schema_version": 1, "seed": 1.5})").message ==
        "expected integer, got number");
  CHECK(only_error(R"({"schema_version": 1, "profile": "Classic"})").path == "$.profile");
  CHECK(only_error(R"({"schema_version": 1, "$schema": 3})").path == "$.$schema");
  CHECK(only_error("[1, 2]") ==
        ValidationIssue{Severity::error, "$", "expected object, got array"});

  const auto big = load_params(R"({"schema_version": 1, "seed": 18446744073709551615})");
  REQUIRE(big.has_value());
  CHECK(big->seed == std::uint64_t{18446744073709551615ULL});
  CHECK(load_params(R"({"$schema": "../../schema/params.schema.json", "schema_version": 1})")
            .has_value());
}

TEST_CASE("params.load newer versions and malformed text are errors, not issues") {
  const auto future = read_params(R"({"schema_version": 2})");
  REQUIRE(!future.has_value());
  CHECK(future.error().code == StatusCode::unsupported_version);
  const auto broken = load_params(R"({"schema_version": 1,)");
  REQUIRE(!broken.has_value());
  CHECK(broken.error().code == StatusCode::parse_error);
}

TEST_CASE("params.load every error is reported, cross-field rules only on clean input") {
  const auto issues = issues_of(
      R"({"schema_version": 1, "grid": {"size": "x", "dx_m": -5},
          "fields": {"rainfall": {"min": 80, "max": 20}}})");
  REQUIRE(issues.size() == 2);  // the rainfall range is checked once the types are right
  CHECK(issues[0].path == "$.grid.size");
  CHECK(issues[1].path == "$.grid.dx_m");
  const auto loaded = load_params(R"({"schema_version": 1, "grid": {"size": "x", "dx_m": -5}})");
  REQUIRE(!loaded.has_value());
  CHECK(loaded.error().message ==
        "invalid parameters:\n$.grid.size: expected integer, got string\n"
        "$.grid.dx_m: -5 is below the minimum 1");
}

TEST_CASE("params.load warnings do not refuse") {
  const std::string_view json =
      R"({"schema_version": 1, "checks": {"PEAK_NUMBER_MIN": 3},
          "fields": {"elevation": {"max": 300}}})";
  const auto issues = issues_of(json);
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].severity == Severity::warning);
  CHECK(load_params(json).has_value());
}

TEST_CASE("params.load files") {
  const auto missing = wg::params::load_params_file(wg::test::source_path("no/such/params.json"));
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == StatusCode::io_error);

  const std::string text = wg::test::read_source_file("data/presets/df_default.json");
  const auto from_file =
      wg::params::load_params_file(wg::test::source_path("data/presets/df_default.json"));
  const auto from_buffer = load_params(text);
  REQUIRE(from_file.has_value());
  REQUIRE(from_buffer.has_value());
  CHECK(*from_file == *from_buffer);
}

TEST_CASE("params.load every shipped preset validates") {
  const std::vector<std::filesystem::path> presets = wg::test::json_files("data/presets");
  REQUIRE(presets.size() >= 3);
  for (const std::filesystem::path& path : presets) {
    CAPTURE(path.generic_string());
    const auto text = wg::io::read_text_file(path);
    REQUIRE(text.has_value());
    const auto result = read_params(*text);
    REQUIRE(result.has_value());
    CHECK(wg::params::format_issues(result->issues) == "");
  }
}

TEST_CASE("params.load to_json round trip keeps the document") {
  wg::params::ParamsDocument doc;
  doc.seed = 99;
  doc.profile = "physical";
  doc.params.grid.dx_m = 1000.0 / 3.0;  // a double without a short decimal form
  doc.params.poles = wg::params::Poles::south;
  doc.params.fields.temperature.weights = {0.5, 1e-7, 3.0, 0.0, 1.0};
  doc.params.precipitation.df_drainage_quirk = false;
  const auto text = wg::params::to_json(doc);
  REQUIRE(text.has_value());
  CHECK(text->find("\"schema_version\": 1") != std::string::npos);
  CHECK(text->find("\"poles\": \"south\"") != std::string::npos);
  CHECK(text->find("\"EROSION_CYCLE_COUNT\": 250") != std::string::npos);
  const auto back = load_params(*text);
  REQUIRE(back.has_value());
  CHECK(*back == doc);
}
