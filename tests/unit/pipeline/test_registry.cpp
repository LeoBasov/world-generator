#include "wg/pipeline/registry.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "support/mock_stages.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::pipeline::StageRegistry;
using wg::test::mock;

TEST_CASE("pipeline.registry validates ids") {
  CHECK(wg::pipeline::is_valid_id("c01_allocate"));
  CHECK(wg::pipeline::is_valid_id("p11_verify"));
  CHECK(wg::pipeline::is_valid_id("x"));
  CHECK_FALSE(wg::pipeline::is_valid_id(""));
  CHECK_FALSE(wg::pipeline::is_valid_id("01_allocate"));
  CHECK_FALSE(wg::pipeline::is_valid_id("_x"));
  CHECK_FALSE(wg::pipeline::is_valid_id("C01"));
  CHECK_FALSE(wg::pipeline::is_valid_id("c01-allocate"));
  CHECK_FALSE(wg::pipeline::is_valid_id("c01 allocate"));
}

TEST_CASE("pipeline.registry adds and creates stages") {
  StageRegistry r;
  CHECK(r.size() == 0);
  REQUIRE(wg::test::add_mock(r, mock("m02_b", {}, {FieldId::elevation})).ok());
  REQUIRE(wg::test::add_mock(r, mock("m01_a", {}, {FieldId::latitude})).ok());
  CHECK(r.size() == 2);
  CHECK(r.contains("m01_a"));
  CHECK_FALSE(r.contains("m03_c"));
  CHECK(r.ids() == std::vector<std::string_view>{"m01_a", "m02_b"});

  auto s = r.create("m02_b");
  REQUIRE(s);
  CHECK((*s)->id() == "m02_b");
  CHECK((*s)->outputs() == wg::pipeline::FieldSet{FieldId::elevation});
  // Every create makes a new instance.
  auto t = r.create("m02_b");
  REQUIRE(t);
  CHECK(s->get() != t->get());
}

TEST_CASE("pipeline.registry refuses bad registrations") {
  StageRegistry r;
  REQUIRE(wg::test::add_mock(r, mock("m01_a", {}, {})).ok());
  CHECK(wg::test::add_mock(r, mock("m01_a", {}, {})).code == StatusCode::already_exists);
  CHECK(wg::test::add_mock(r, mock("Bad", {}, {})).code == StatusCode::invalid_argument);
  CHECK(r.add("m02_b", StageRegistry::Factory{}).code == StatusCode::invalid_argument);
  CHECK(r.size() == 1);
}

TEST_CASE("pipeline.registry create reports missing and broken factories") {
  StageRegistry r;
  CHECK(r.create("m01_a").error().code == StatusCode::not_found);

  REQUIRE(r.add("m01_null", [] { return std::unique_ptr<wg::pipeline::Stage>(); }).ok());
  CHECK(r.create("m01_null").error().code == StatusCode::internal);

  REQUIRE(r.add("m02_throws", []() -> std::unique_ptr<wg::pipeline::Stage> {
             throw std::runtime_error("boom");
           }).ok());
  auto thrown = r.create("m02_throws");
  REQUIRE_FALSE(thrown);
  CHECK(thrown.error().code == StatusCode::internal);
  CHECK(thrown.error().message == "stage m02_throws: factory threw: boom");

  // The id keys the RNG, so a factory must make the stage it is registered for.
  auto other = mock("m03_other", {}, {});
  REQUIRE(
      r.add("m03_wrong", [other] { return std::make_unique<wg::test::MockStage>(other); }).ok());
  auto wrong = r.create("m03_wrong");
  REQUIRE_FALSE(wrong);
  CHECK(wrong.error().code == StatusCode::internal);
  CHECK(wrong.error().message == "stage m03_wrong: factory made a stage with id m03_other");
}
