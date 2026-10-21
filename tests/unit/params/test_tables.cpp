#include "wg/params/tables.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "io/json.hpp"
#include "support/source_files.hpp"

namespace {

using wg::core::StatusCode;
using wg::io::Json;
using wg::params::DataTables;
using wg::params::TableSources;

constexpr std::string_view kBiomes = "data/tables/biome_table.json";
constexpr std::string_view kClimate = "data/tables/climate_classes.json";
constexpr std::string_view kSalinity = "data/tables/salinity_classes.json";
constexpr std::string_view kRocks = "data/geology/rock_types.json";
constexpr std::string_view kSoils = "data/geology/soils.json";
constexpr std::string_view kMinerals = "data/geology/minerals.json";

// The shipped tables as text; a test may replace one.
struct Texts {
  std::string biomes = wg::test::read_source_file(kBiomes);
  std::string climate = wg::test::read_source_file(kClimate);
  std::string salinity = wg::test::read_source_file(kSalinity);
  std::string rocks = wg::test::read_source_file(kRocks);
  std::string soils = wg::test::read_source_file(kSoils);
  std::string minerals = wg::test::read_source_file(kMinerals);

  [[nodiscard]] TableSources sources() const {
    return TableSources{biomes, climate, salinity, rocks, soils, minerals};
  }
};

// A shipped table with one change applied.
std::string mutated(std::string_view relative, const std::function<void(Json&)>& change) {
  auto json = wg::io::parse_json(wg::test::read_source_file(relative));
  REQUIRE(json.has_value());
  change(*json);
  auto text = wg::io::dump_json(*json);
  REQUIRE(text.has_value());
  return *text;
}

// The message of a refused table; it must be refused as invalid.
template <class T>
std::string refusal(const wg::core::Expected<T>& result) {
  REQUIRE(!result.has_value());
  CHECK(result.error().code == StatusCode::invalid_argument);
  return result.error().message;
}

bool contains(const std::string& text, std::string_view part) {
  return text.find(part) != std::string::npos;
}

DataTables shipped() {
  const auto tables = wg::params::load_tables_dir(wg::test::source_path("data"));
  REQUIRE(tables.has_value());
  return *tables;
}

}  // namespace

TEST_CASE("params.tables every shipped table loads and validates") {
  const DataTables t = shipped();
  CHECK(wg::params::validate_tables(t).empty());
  CHECK(t.biomes.biomes.size() == 42);
  CHECK(t.climate.classes.size() == 6);
  CHECK(t.salinity.classes.size() == 4);
  CHECK(t.rocks.rocks.size() == 25);
  CHECK(t.soils.soils.size() == 16);
  CHECK(!t.minerals.minerals.empty());
}

TEST_CASE("params.tables buffers and directory give the same tables") {
  const Texts texts;
  const auto from_buffers = wg::params::load_tables(texts.sources());
  REQUIRE(from_buffers.has_value());
  CHECK(*from_buffers == shipped());
  const auto missing = wg::params::load_tables_dir(wg::test::source_path("no/such/dir"));
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == StatusCode::io_error);
}

TEST_CASE("params.tables biome table contents") {
  const wg::params::BiomeTable& b = shipped().biomes;
  const wg::params::Biome* mountain = b.find("MOUNTAIN");
  REQUIRE(mountain != nullptr);
  CHECK(mountain->id == 0);
  CHECK(b.mountain == 0);
  CHECK(b.glacier == b.find("GLACIER")->id);
  CHECK(b.mangrove.biome == b.find("SWAMP_MANGROVE")->id);
  CHECK(b.find("NOPE") == nullptr);
  CHECK(b.thresholds.ocean_max_elevation == 99);
  CHECK(b.thresholds.mountain_min_elevation == 300);
  CHECK(b.base.size() == 14);
  CHECK(b.variants.front().biome == b.find("DESERT_SAND")->id);
  CHECK(b.region_sizes.back().min_tiles == 100);
}

TEST_CASE("params.tables climate and salinity lookups") {
  const DataTables t = shipped();
  CHECK(t.climate.classes[t.climate.classify(-1000.0)].name == "freezing");
  CHECK(t.climate.classes[t.climate.classify(0.0)].name == "freezing");
  CHECK(t.climate.classes[t.climate.classify(20.0)].name == "temperate");
  CHECK(t.climate.classes[t.climate.classify(150.0)].name == "scorching");
  CHECK(t.salinity.quantise(0.0) == 0);
  CHECK(t.salinity.quantise(20.0) == 33);
  CHECK(t.salinity.quantise(60.0) == 66);
  CHECK(t.salinity.quantise(100.0) == 100);
  CHECK(t.rocks.find("granite")->rock_class == wg::params::RockClass::igneous_intrusive);
  CHECK(t.rocks.find("unobtainium") == nullptr);
}

TEST_CASE("params.tables hash is stable and sees every change") {
  const DataTables t = shipped();
  const std::uint64_t h = wg::params::tables_hash(t);
  CHECK(wg::params::tables_hash(shipped()) == h);
  DataTables changed = t;
  changed.rocks.rocks[3].erodibility *= 2.0;
  CHECK(wg::params::tables_hash(changed) != h);
  changed = t;
  changed.biomes.base[0].confidence = wg::params::Confidence::documented;
  CHECK(wg::params::tables_hash(changed) != h);
  changed = t;
  changed.minerals.minerals[0].environments[0].frequency += 1;
  CHECK(wg::params::tables_hash(changed) != h);
}

TEST_CASE("params.tables biome base rows must partition rainfall x drainage") {
  const std::string overlap = refusal(wg::params::load_biome_table(mutated(kBiomes, [](Json& j) {
    j["base"][0]["rainfall"] = Json::array({0, 10});
  })));
  CHECK(contains(overlap, "$.base[3]: overlaps $.base[0] at rainfall 10, drainage 0"));
  const std::string gap = refusal(wg::params::load_biome_table(mutated(kBiomes, [](Json& j) {
    j["base"][0]["rainfall"] = Json::array({0, 8});
  })));
  CHECK(contains(gap, "$.base: rainfall 9, drainage 0 is covered by no row"));
  const std::string reversed = refusal(wg::params::load_biome_table(mutated(kBiomes, [](Json& j) {
    j["base"][0]["drainage"] = Json::array({32, 0});
  })));
  CHECK(contains(reversed, "$.base[0].drainage: lower end 32 is above upper end 0"));
}

TEST_CASE("params.tables biome ids, names and references") {
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["biomes"][5]["id"] = 4; }))),
                 "$.biomes[5].id: is 4, expected 5"));
  // FOREST_TROPICAL_DRY_BROADLEAF (16) is the one biome no rule names, so renaming it leaves
  // every reference intact and only the duplicate remains.
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["biomes"][16]["name"] = "FOREST_TAIGA"; }))),
                 "$.biomes[16].name: duplicate name \"FOREST_TAIGA\""));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["variants"][0]["biome"] = "NOPE"; }))),
                 "$.variants[0].biome: unknown biome \"NOPE\""));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["biomes"][0]["region_type"] = "moon"; }))),
                 "$.biomes[0].region_type: unknown region type \"moon\""));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes,
                             [](Json& j) {
                               Json& types = j["region_types"];
                               types.erase(types.begin() + 6);  // "hills": no biome uses it
                             }))),
                 "$.region_types: missing region type \"hills\" of $.checks.REGION_COUNTS.HILLS"));
}

TEST_CASE("params.tables biome variants cover every base and band") {
  CHECK(contains(refusal(wg::params::load_biome_table(mutated(
                     kBiomes, [](Json& j) { j["variants"].erase(j["variants"].begin()); }))),
                 "no variant of base \"desert_sand\" in band \"cold\""));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["variants"].push_back(j["variants"][0]); }))),
                 "second variant of base \"desert_sand\" in band \"cold\""));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["variants"][0]["saline"] = true; }))),
                 "$.variants[0].saline: only wetland variants have saline"));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["variants"][0]["bands"][0] = "frozen"; }))),
                 "$.variants[0].bands[0]: frozen band \"frozen\" has no variants"));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["mangrove"]["base"] = "badlands"; }))),
                 "$.mangrove.base: \"badlands\" is not a wetland base"));
}

TEST_CASE("params.tables temperature bands and water rows") {
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes,
                             [](Json& j) {
                               j["temperature_bands"][1]["range"] = Json::array({-3, 9});
                             }))),
                 "$.temperature_bands[1].range: starts at -3, expected -4"));
  CHECK(contains(refusal(wg::params::load_biome_table(
                     mutated(kBiomes, [](Json& j) { j["ocean"].erase(j["ocean"].begin()); }))),
                 "$.ocean: no row for band \"frozen\""));
  CHECK(contains(refusal(wg::params::load_biome_table(mutated(
                     kBiomes, [](Json& j) { j["lakes"][1]["salinity"] = Json::array({0}); }))),
                 "second row for band \"frozen\" and salinity 0"));
  // Lake salinities must cover the salinity classes: a rule across tables.
  Texts texts;
  texts.biomes = mutated(kBiomes, [](Json& j) { j["lakes"][2]["salinity"] = Json::array({66}); });
  REQUIRE(wg::params::load_biome_table(texts.biomes).has_value());
  CHECK(contains(refusal(wg::params::load_tables(texts.sources())),
                 "biome_table:$.lakes: no row for band \"frozen\" and salinity 100"));
}

TEST_CASE("params.tables climate and salinity rules") {
  CHECK(contains(refusal(wg::params::load_climate_classes(
                     mutated(kClimate, [](Json& j) { j["classes"][2]["min_value"] = 0; }))),
                 "$.classes[2].min_value: classes must be sorted by increasing min_value"));
  CHECK(contains(refusal(wg::params::load_climate_classes(
                     mutated(kClimate, [](Json& j) { j["classes"][0]["min_value"] = -999; }))),
                 "$.classes[0].min_value: the first class must start at"));
  CHECK(contains(refusal(wg::params::load_salinity_classes(
                     mutated(kSalinity, [](Json& j) { j["classes"][1]["value"] = 30; }))),
                 "$.classes: class values must be the salinity field values 0, 33, 66, 100"));
}

TEST_CASE("params.tables geology rules") {
  CHECK(contains(refusal(wg::params::load_rock_types(
                     mutated(kRocks, [](Json& j) { j["rocks"][1]["name"] = "sandstone"; }))),
                 "$.rocks[1].name: duplicate name \"sandstone\""));
  CHECK(contains(refusal(wg::params::load_rock_types(
                     mutated(kRocks, [](Json& j) { j["rocks"][0]["extra"] = 1; }))),
                 "$.rocks[0].extra: unknown key"));
  CHECK(contains(refusal(wg::params::load_rock_types(
                     mutated(kRocks, [](Json& j) { j["rocks"][0]["erodibility"] = 0; }))),
                 "$.rocks[0].erodibility: must be positive"));
  CHECK(contains(refusal(wg::params::load_soils(mutated(kSoils,
                                                        [](Json& j) {
                                                          j["soils"][0]["land"] = false;
                                                          j["soils"][0]["shallow_ocean"] = false;
                                                        }))),
                 "$.soils[0]: needs at least one of land, shallow_ocean, deep_ocean"));
  CHECK(
      contains(refusal(wg::params::load_minerals(mutated(
                   kMinerals,
                   [](Json& j) { j["minerals"][0]["environments"][0]["host_rock"] = "granite"; }))),
               "needs exactly one of host_class, host_rock, host_mineral"));
  CHECK(contains(
      refusal(wg::params::load_minerals(mutated(kMinerals,
                                                [](Json& j) {
                                                  Json& minerals = j["minerals"];
                                                  Json& last = minerals[minerals.size() - 1];
                                                  last["environments"][0]["host_mineral"] =
                                                      last["name"];
                                                }))),
      "a mineral cannot host itself"));
  // A rock named by a mineral must exist: a rule across tables.
  Texts texts;
  texts.minerals = mutated(
      kMinerals, [](Json& j) { j["minerals"][4]["environments"][0]["host_rock"] = "unobtainium"; });
  REQUIRE(wg::params::load_minerals(texts.minerals).has_value());
  CHECK(contains(refusal(wg::params::load_tables(texts.sources())),
                 "minerals:$.minerals[4].environments[0].host_rock: unknown rock \"unobtainium\""));
}

TEST_CASE("params.tables versions and malformed text") {
  const auto future =
      wg::params::load_soils(mutated(kSoils, [](Json& j) { j["schema_version"] = 2; }));
  REQUIRE(!future.has_value());
  CHECK(future.error().code == StatusCode::unsupported_version);
  const auto broken = wg::params::load_soils("{");
  REQUIRE(!broken.has_value());
  CHECK(broken.error().code == StatusCode::parse_error);
  CHECK(contains(broken.error().message, "soils: "));
  CHECK(contains(refusal(wg::params::load_soils("[]")), "$: expected object, got array"));
  CHECK(contains(refusal(wg::params::load_soils(R"({"schema_version": 1, "soils": []})")),
                 "$.soils: must not be empty"));
  // The first table that fails names itself.
  Texts texts;
  texts.rocks = "{";
  const auto tables = wg::params::load_tables(texts.sources());
  REQUIRE(!tables.has_value());
  CHECK(contains(tables.error().message, "rock_types: "));
}
