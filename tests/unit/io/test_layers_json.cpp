#include "io/layers_json.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>

#include "io/json.hpp"
#include "support/sample_world.hpp"

using wg::core::StatusCode;
using wg::io::Json;
using wg::io::detail::decode_layers;
using wg::io::detail::encode_layers;

namespace {

Json encoded() {
  auto j = encode_layers(wg::test::sample_layers());
  REQUIRE(j);
  return *j;
}

// Decodes j and returns the error message (empty on success).
std::string decode_error(const Json& j) {
  auto layers = decode_layers(j, "$.layers");
  if (layers) {
    return {};
  }
  CHECK(layers.error().code == StatusCode::parse_error);
  return layers.error().message;
}

}  // namespace

TEST_CASE("io.layers_json round trip keeps every bit") {
  const wg::core::VectorLayers layers = wg::test::sample_layers();
  const Json j = encoded();
  // Through text as well, the way the world file stores it.
  auto text = wg::io::dump_json(j, -1);
  REQUIRE(text);
  auto parsed = wg::io::parse_json(*text);
  REQUIRE(parsed);
  auto back = decode_layers(*parsed, "$");
  REQUIRE(back);
  CHECK(*back == layers);
  CHECK(std::signbit(back->peaks[1].height_eu));
  CHECK(back->peaks[1].radius_cells == std::numeric_limits<float>::denorm_min());
  CHECK(back->river_graph.discharge_m3_yr[2] == layers.river_graph.discharge_m3_yr[2]);
}

TEST_CASE("io.layers_json uses member names and labels") {
  const Json j = encoded();
  CHECK(j["plates"][0]["type"] == "oceanic");
  CHECK(j["plates"][0]["boundaries"][0]["boundary_class"] == "divergent");
  CHECK(j["minerals"][0]["inclusion"] == "cluster_small");
  CHECK(j["river_graph"]["downstream"][2] == -1);
  CHECK(j["volcanoes"][0]["magma_connected"] == true);
  auto empty = encode_layers(wg::core::VectorLayers{});
  REQUIRE(empty);
  auto back = decode_layers(*empty, "$");
  REQUIRE(back);
  CHECK(back->empty());
}

TEST_CASE("io.layers_json encoding refuses non-finite numbers and ragged river graphs") {
  wg::core::VectorLayers layers = wg::test::sample_layers();
  layers.plates[0].boundaries[0].shear = std::numeric_limits<double>::infinity();
  auto j = encode_layers(layers);
  REQUIRE_FALSE(j);
  CHECK(j.error().code == StatusCode::invalid_argument);
  CHECK(j.error().message == "layer plates[0].boundaries.shear is not finite");

  layers = wg::test::sample_layers();
  layers.river_graph.order.pop_back();
  j = encode_layers(layers);
  REQUIRE_FALSE(j);
  CHECK(j.error().message == "layer river_graph: arrays differ in length");
}

TEST_CASE("io.layers_json decoding reports problems with their paths") {
  Json j = encoded();
  j["peaks"][0]["cell"] = "five";
  CHECK(decode_error(j).find("$.layers.peaks[0].cell: expected integer, got string") !=
        std::string::npos);

  j = encoded();
  j["lakes"][0]["salinity_class"] = 300;
  CHECK(decode_error(j).find("$.layers.lakes[0].salinity_class: 300 is above the maximum 255") !=
        std::string::npos);

  j = encoded();
  j["plates"][0]["type"] = "continentalish";
  CHECK(decode_error(j).find("$.layers.plates[0].type") != std::string::npos);

  j = encoded();
  j["minerals"][0].erase("cell");
  CHECK(decode_error(j).find("$.layers.minerals[0].cell: missing required key") !=
        std::string::npos);

  j = encoded();
  j["regions"][0]["extra"] = 1;
  CHECK(decode_error(j).find("$.layers.regions[0].extra: unknown key") != std::string::npos);

  j = encoded();
  j["river_graph"]["cell"][1] = -1.5;
  CHECK(decode_error(j).find("$.layers.river_graph.cell[1]: expected integer, got number") !=
        std::string::npos);

  j = encoded();
  j["river_graph"]["width_class"][0] = 256;
  CHECK(decode_error(j).find("$.layers.river_graph.width_class[0]: 256 is above the maximum 255") !=
        std::string::npos);

  j = encoded();
  j["river_graph"]["discharge_m3_yr"][0] = 1e300;
  CHECK(decode_error(j).find("$.layers.river_graph.discharge_m3_yr[0]") != std::string::npos);

  j = encoded();
  j["river_graph"]["name_id"].push_back(1);
  CHECK(decode_error(j).find("$.layers.river_graph: arrays differ in length") != std::string::npos);

  CHECK(decode_error(Json::array()).find("$.layers: expected object, got array") !=
        std::string::npos);
}
