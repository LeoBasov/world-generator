#include "wg/pipeline/profile.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/source_files.hpp"

using wg::core::StatusCode;
using wg::pipeline::load_profile;
using wg::pipeline::Profile;

namespace {

// Message of a refused profile, which must be invalid_argument.
std::string refusal(const std::string& json) {
  auto p = load_profile(json);
  REQUIRE_FALSE(p);
  CHECK(p.error().code == StatusCode::invalid_argument);
  return p.error().message;
}

}  // namespace

TEST_CASE("pipeline.profile loads the mock fixture") {
  auto p =
      wg::pipeline::load_profile_file(wg::test::source_path("tests/fixtures/profiles/mock.json"));
  REQUIRE(p);
  CHECK(*p == Profile{.name = "mock", .stages = {"m01_seed", "m02_shape", "m03_verify"}});
}

TEST_CASE("pipeline.profile loads a buffer") {
  auto p = load_profile(R"({"schema_version": 1, "name": "classic", "stages": ["c01_allocate"]})");
  REQUIRE(p);
  CHECK(p->name == "classic");
  CHECK(p->stages == std::vector<std::string>{"c01_allocate"});
}

TEST_CASE("pipeline.profile refuses malformed profiles with their paths") {
  CHECK(refusal(R"({"name": "x", "stages": ["a"]})") ==
        "invalid profile:\n$.schema_version: missing required key");
  CHECK(refusal(R"({"schema_version": 0, "name": "x", "stages": ["a"]})")
            .find("$.schema_version: ") != std::string::npos);
  CHECK(refusal(R"({"schema_version": 1, "name": "X", "stages": ["a"]})") ==
        "invalid profile:\n$.name: \"X\" is not a profile id ([a-z][a-z0-9_]*)");
  CHECK(refusal(R"({"schema_version": 1, "name": "x", "stages": []})") ==
        "invalid profile:\n$.stages: a profile needs at least one stage");
  CHECK(refusal(R"({"schema_version": 1, "name": "x", "stages": ["a", 3, "b-c", "a"]})") ==
        "invalid profile:\n"
        "$.stages[1]: expected string, got integer\n"
        "$.stages[2]: \"b-c\" is not a stage id ([a-z][a-z0-9_]*)\n"
        "$.stages[3]: stage a is listed twice");
  CHECK(refusal(R"({"schema_version": 1, "name": "x", "stages": "a"})") ==
        "invalid profile:\n$.stages: expected array, got string");
  CHECK(refusal(R"({"schema_version": 1, "name": "x", "stages": ["a"], "extra": 1})")
            .find("$.extra") != std::string::npos);
  CHECK(refusal("[]").find("$: expected object") != std::string::npos);
}

TEST_CASE("pipeline.profile accepts $schema and description") {
  auto p = load_profile(
      R"({"$schema": "../../schema/profile.schema.json", "schema_version": 1, "name": "x",
          "description": "test", "stages": ["a"]})");
  CHECK(p);
}

TEST_CASE("pipeline.profile reports parse, version and file errors") {
  CHECK(load_profile("{").error().code == StatusCode::parse_error);
  auto newer = load_profile(R"({"schema_version": 2, "name": "x", "stages": ["a"]})");
  REQUIRE_FALSE(newer);
  CHECK(newer.error().code == StatusCode::unsupported_version);
  CHECK(wg::pipeline::load_profile_file(wg::test::source_path("tests/fixtures/profiles/none.json"))
            .error()
            .code == StatusCode::io_error);
}
