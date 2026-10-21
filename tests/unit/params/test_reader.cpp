#include "params/reader.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using wg::io::Json;
using wg::params::Severity;
using wg::params::ValidationIssue;
using wg::params::detail::format_number;
using wg::params::detail::ObjectReader;
using wg::params::detail::read_number;

namespace {

Json parse(std::string_view text) {
  auto j = wg::io::parse_json(text);
  REQUIRE(j.has_value());
  return *j;
}

}  // namespace

TEST_CASE("params.reader numbers for messages") {
  CHECK(format_number(1.0) == "1");
  CHECK(format_number(-2.0) == "-2");
  CHECK(format_number(0.5) == "0.5");
  CHECK(format_number(1e-6) == "1e-06");
  CHECK(format_number(0.002) == "0.002");
  CHECK(format_number(1e20) == "1e+20");
}

TEST_CASE("params.reader read_number checks type and bounds") {
  std::vector<ValidationIssue> issues;
  CHECK(read_number(Json(3), "$.a", 0, 5, true, issues) == 3.0);
  CHECK(read_number(Json(2.5), "$.a", 0, 5, false, issues) == 2.5);
  CHECK(read_number(Json(4), "$.a", 0, 5, false, issues) == 4.0);  // an integer is a number
  CHECK(issues.empty());

  CHECK(!read_number(Json(2.5), "$.a", 0, 5, true, issues));
  CHECK(!read_number(Json("x"), "$.b", 0, 5, false, issues));
  CHECK(!read_number(Json(-1), "$.c", 0, 5, true, issues));
  CHECK(!read_number(Json(6.5), "$.d", 0, 5, false, issues));
  REQUIRE(issues.size() == 4);
  CHECK(issues[0] == ValidationIssue{Severity::error, "$.a", "expected integer, got number"});
  CHECK(issues[1] == ValidationIssue{Severity::error, "$.b", "expected number, got string"});
  CHECK(issues[2] == ValidationIssue{Severity::error, "$.c", "-1 is below the minimum 0"});
  CHECK(issues[3] == ValidationIssue{Severity::error, "$.d", "6.5 is above the maximum 5"});
}

TEST_CASE("params.reader object reader reports missing and unknown keys") {
  const Json j = parse(R"({"$schema": "x", "name": "a", "n": 2, "flag": true, "extra": 1})");
  std::vector<ValidationIssue> issues;
  ObjectReader r(j, "$.t", issues);
  CHECK(r.valid());
  CHECK(r.string("name") == "a");
  CHECK(r.integer("n", 0, 10) == 2);
  CHECK(r.boolean("flag") == true);
  CHECK(!r.number("missing_optional", 0, 1, false));
  CHECK(!r.number("missing", 0, 1));
  r.finish();
  REQUIRE(issues.size() == 2);
  CHECK(issues[0] == ValidationIssue{Severity::error, "$.t.missing", "missing required key"});
  CHECK(issues[1] == ValidationIssue{Severity::error, "$.t.extra", "unknown key"});
}

TEST_CASE("params.reader object reader type checks") {
  const Json j = parse(R"({"s": 1, "b": "no", "a": {}, "o": [], "l": "west", "w": [1, 2]})");
  std::vector<ValidationIssue> issues;
  ObjectReader r(j, "$", issues);
  CHECK(!r.string("s"));
  CHECK(!r.boolean("b"));
  CHECK(r.array("a") == nullptr);
  CHECK(r.object("o") == nullptr);
  CHECK(!r.label("l", {"north", "south"}));
  CHECK(r.array("w", 3) == nullptr);
  r.finish();
  REQUIRE(issues.size() == 6);
  CHECK(issues[0].message == "expected string, got integer");
  CHECK(issues[1].message == "expected boolean, got string");
  CHECK(issues[2].message == "expected array, got object");
  CHECK(issues[3].message == "expected object, got array");
  CHECK(issues[4].message == "\"west\" is not one of north, south");
  CHECK(issues[5].message == "expected 3 elements, got 2");
}

TEST_CASE("params.reader intervals") {
  const Json j = parse(R"({"ok": [2, 5], "rev": [5, 2], "out": [0, 200]})");
  std::vector<ValidationIssue> issues;
  ObjectReader r(j, "$", issues);
  CHECK(r.interval("ok", 0, 100) == std::pair<std::int64_t, std::int64_t>{2, 5});
  CHECK(!r.interval("rev", 0, 100));
  CHECK(!r.interval("out", 0, 100));
  REQUIRE(issues.size() == 2);
  CHECK(issues[0] == ValidationIssue{Severity::error, "$.rev", "lower end 5 is above upper end 2"});
  CHECK(issues[1] == ValidationIssue{Severity::error, "$.out[1]", "200 is above the maximum 100"});
}

TEST_CASE("params.reader a non-object is reported once") {
  std::vector<ValidationIssue> issues;
  const Json not_an_object = Json::array();
  ObjectReader r(not_an_object, "$.x", issues);
  CHECK(!r.valid());
  CHECK(!r.string("a"));
  r.finish();
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].message == "expected object, got array");
}

TEST_CASE("params.reader warnings keep their severity") {
  std::vector<ValidationIssue> issues;
  wg::params::detail::add_warning(issues, "$.w", "careful");
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].severity == Severity::warning);
}
