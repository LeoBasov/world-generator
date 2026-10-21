#include "io/json.hpp"

#include <doctest/doctest.h>

#include <string>

#include "support/source_files.hpp"

using wg::core::StatusCode;
using wg::io::Json;

TEST_CASE("io.json parses a document and keeps its key order") {
  const auto json = wg::io::parse_json(R"({"b": 1, "a": [true, "x"], "c": 2.5})");
  REQUIRE(json.has_value());
  REQUIRE(json->is_object());
  auto it = json->begin();
  CHECK(it.key() == "b");
  ++it;
  CHECK(it.key() == "a");
  CHECK((*json)["a"][0].get<bool>());
  CHECK((*json)["c"].get<double>() == 2.5);
}

TEST_CASE("io.json malformed text is a parse error with the byte offset") {
  const auto json = wg::io::parse_json(R"({"a": 1,})");
  REQUIRE(!json.has_value());
  CHECK(json.error().code == StatusCode::parse_error);
  CHECK(json.error().message.find("at byte ") != std::string::npos);
  CHECK(!wg::io::parse_json("").has_value());
}

TEST_CASE("io.json dump writes doubles that read back exactly") {
  Json j = Json::object();
  j["x"] = 0.1;
  j["y"] = 1e-300;
  j["z"] = 1000.0;
  j["n"] = 7;
  const auto text = wg::io::dump_json(j);
  REQUIRE(text.has_value());
  CHECK(text->back() == '\n');
  const auto back = wg::io::parse_json(*text);
  REQUIRE(back.has_value());
  CHECK((*back)["x"].get<double>() == 0.1);
  CHECK((*back)["y"].get<double>() == 1e-300);
  CHECK((*back)["z"].get<double>() == 1000.0);
  CHECK((*back)["n"].is_number_integer());
}

TEST_CASE("io.json dump replaces invalid UTF-8 instead of failing") {
  Json j = Json::object();
  j["s"] = std::string(
      "a\xff"
      "b");
  const auto text = wg::io::dump_json(j, -1);
  REQUIRE(text.has_value());
  CHECK(text->find("\xef\xbf\xbd") != std::string::npos);
}

TEST_CASE("io.json read_text_file reads bytes and reports a missing file") {
  const auto text = wg::io::read_text_file(wg::test::source_path("schema/params.schema.json"));
  REQUIRE(text.has_value());
  CHECK(text->find("\"schema_version\"") != std::string::npos);
  const auto missing = wg::io::read_text_file(wg::test::source_path("no/such/file.json"));
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == StatusCode::io_error);
  CHECK(missing.error().message.find("file.json") != std::string::npos);
}

TEST_CASE("io.json type names and paths for messages") {
  CHECK(wg::io::json_type_name(Json(true)) == "boolean");
  CHECK(wg::io::json_type_name(Json(3)) == "integer");
  CHECK(wg::io::json_type_name(Json(3.5)) == "number");
  CHECK(wg::io::json_type_name(Json("s")) == "string");
  CHECK(wg::io::json_type_name(Json::array()) == "array");
  CHECK(wg::io::json_type_name(Json::object()) == "object");
  CHECK(wg::io::json_type_name(Json()) == "null");
  CHECK(wg::io::json_child_path("$", "grid") == "$.grid");
  CHECK(wg::io::json_index_path("$.w", 2) == "$.w[2]");
}
