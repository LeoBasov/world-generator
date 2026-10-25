#include "wg/classify/biome.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/water.hpp"

using wg::classify::BiomeClassifier;
using wg::classify::CellClass;
using wg::classify::CellInputs;
using wg::core::StatusCode;
using wg::hydro::WaterClass;
using wg::params::BiomeId;
using wg::params::BiomeTable;

namespace {

const BiomeTable& table() { return wg::test::repo_tables().biomes; }

const BiomeClassifier& classifier() {
  static const BiomeClassifier c = [] {
    auto created = BiomeClassifier::create(table());
    REQUIRE(created);
    return *created;
  }();
  return c;
}

BiomeId id_of(std::string_view name) {
  const wg::params::Biome* b = table().find(name);
  REQUIRE(b != nullptr);
  return b->id;
}

std::uint8_t type_of(std::string_view name) {
  const auto& types = table().region_types;
  const auto it = std::find(types.begin(), types.end(), name);
  REQUIRE(it != types.end());
  return static_cast<std::uint8_t>(it - types.begin());
}

std::uint8_t u8(WaterClass w) { return static_cast<std::uint8_t>(w); }

std::int32_t floor_clamped(double v, std::int32_t lo, std::int32_t hi) {
  if (!(v >= lo)) {
    return lo;
  }
  return v >= hi ? hi : static_cast<std::int32_t>(std::floor(v));
}

bool lists(const std::vector<std::string>& bands, const std::string& band) {
  return std::find(bands.begin(), bands.end(), band) != bands.end();
}

// R9.1 straight from the table rows, without any lookup table.
CellClass reference(const CellInputs& in) {
  const BiomeTable& t = table();
  const std::int32_t temp = floor_clamped(in.temperature, -1000, 1000);
  const auto band = std::find_if(t.temperature_bands.begin(), t.temperature_bands.end(),
                                 [&](const auto& b) { return b.min <= temp && temp <= b.max; });
  REQUIRE(band != t.temperature_bands.end());
  const auto typed = [&](BiomeId b) { return CellClass{b, type_of(t.biomes[b].region_type)}; };
  const auto water_biome = [&](const std::vector<wg::params::WaterBiome>& rows) {
    std::int32_t best = -1;
    BiomeId biome = 0;
    for (const auto& row : rows) {
      for (const std::int32_t v : row.salinity) {
        if (lists(row.bands, band->name) && v <= in.salinity && v > best) {
          best = v;
          biome = row.biome;
        }
      }
    }
    REQUIRE(best >= 0);
    return biome;
  };
  const double z = std::floor(static_cast<double>(in.elevation));
  if (in.has_water && in.water == u8(WaterClass::ocean)) {
    for (const auto& row : t.ocean) {
      if (lists(row.bands, band->name)) {
        return typed(row.biome);
      }
    }
  }
  if (in.has_water && in.water == u8(WaterClass::lake)) {
    return typed(water_biome(t.lakes));
  }
  if (!in.has_water && z <= t.thresholds.ocean_max_elevation) {
    for (const auto& row : t.ocean) {
      if (lists(row.bands, band->name)) {
        return typed(row.biome);
      }
    }
  }
  if (z >= t.thresholds.mountain_min_elevation) {
    return typed(t.mountain);
  }
  const std::int32_t d = floor_clamped(in.drainage, 0, 100);
  if (band->frozen) {
    return typed(d < t.thresholds.glacier_min_drainage ? t.tundra : t.glacier);
  }
  const std::int32_t p = floor_clamped(in.rainfall, 0, 100);
  const auto row = std::find_if(t.base.begin(), t.base.end(), [&](const auto& r) {
    return r.rainfall_min <= p && p <= r.rainfall_max && r.drainage_min <= d && d <= r.drainage_max;
  });
  REQUIRE(row != t.base.end());
  const bool saline = in.salinity >= t.thresholds.saline_min_salinity;
  BiomeId biome = 0;
  bool found = false;
  for (const auto& v : t.variants) {
    if (v.base == row->base && lists(v.bands, band->name) &&
        (!v.has_saline || v.saline == (row->wetland && saline))) {
      biome = v.biome;
      found = true;
    }
  }
  REQUIRE(found);
  if (row->base == t.mangrove.base && band->name == t.mangrove.band && saline &&
      d <= t.thresholds.mangrove_max_drainage) {
    biome = t.mangrove.biome;
  }
  return {biome, row->hills ? type_of("hills") : type_of(t.biomes[biome].region_type)};
}

// Every bound b of the table as b - 1, b, b + 1 (and a fraction below b + 1).
std::vector<double> edges_of(const std::vector<std::int32_t>& bounds) {
  std::set<double> values;
  for (const std::int32_t b : bounds) {
    for (const double v : {b - 1.0, b - 0.01, b + 0.0, b + 0.5, b + 1.0}) {
      values.insert(v);
    }
  }
  return {values.begin(), values.end()};
}

CellClass land(double p, double d, double t, std::uint8_t s = 0, double z = 150.0) {
  return classifier().classify({.elevation = static_cast<float>(z),
                                .rainfall = static_cast<float>(p),
                                .drainage = static_cast<float>(d),
                                .temperature = static_cast<float>(t),
                                .salinity = s,
                                .water = 0,
                                .has_water = false});
}

}  // namespace

TEST_CASE("classify.BiomeClassifier gives the biome of every row of R9.2") {
  CHECK(land(5, 10, 50).biome == id_of("DESERT_SAND"));
  CHECK(land(5, 40, 50).biome == id_of("DESERT_ROCK"));
  CHECK(land(5, 80, 50).biome == id_of("DESERT_BADLAND"));
  CHECK(land(50, 10, 50).biome == id_of("MARSH_TEMPERATE_FRESHWATER"));
  CHECK(land(80, 10, 50).biome == id_of("SWAMP_TEMPERATE_FRESHWATER"));
  CHECK(land(70, 50, 50).biome == id_of("FOREST_TEMPERATE_CONIFER"));
  CHECK(land(80, 50, 50).biome == id_of("FOREST_TEMPERATE_BROADLEAF"));
  CHECK(land(15, 40, 50).biome == id_of("GRASSLAND_TEMPERATE"));
  CHECK(land(25, 40, 50).biome == id_of("SAVANNA_TEMPERATE"));
  CHECK(land(50, 40, 50).biome == id_of("SHRUBLAND_TEMPERATE"));
  // Hills: drainage >= 66 at rainfall 10-65 keeps the vegetation, changes the region type.
  const CellClass hills = land(15, 80, 50);
  CHECK(hills.biome == id_of("GRASSLAND_TEMPERATE"));
  CHECK(hills.region_type == type_of("hills"));
  CHECK(land(15, 40, 50).region_type == type_of("grassland"));
}

TEST_CASE("classify.BiomeClassifier applies the elevation, freezing and variant rules of R9.1") {
  CHECK(land(50, 50, 50, 0, 99.99).biome == id_of("OCEAN_TEMPERATE"));
  CHECK(land(50, 50, 90, 0, 50).biome == id_of("OCEAN_TROPICAL"));
  CHECK(land(50, 50, -20, 0, 50).biome == id_of("OCEAN_ARCTIC"));
  CHECK(land(50, 50, 50, 0, 300).biome == id_of("MOUNTAIN"));
  CHECK(land(50, 50, -20, 0, 300).biome == id_of("MOUNTAIN"));
  CHECK(land(50, 74, -5).biome == id_of("TUNDRA"));
  CHECK(land(50, 75, -5).biome == id_of("GLACIER"));
  CHECK(land(50, 74, -5).region_type == type_of("tundra"));
  CHECK(land(70, 50, 9).biome == id_of("FOREST_TAIGA"));
  CHECK(land(70, 50, -4).biome == id_of("FOREST_TAIGA"));
  CHECK(land(70, 50, 10).biome == id_of("FOREST_TEMPERATE_CONIFER"));
  CHECK(land(80, 50, 85).biome == id_of("FOREST_TROPICAL_MOIST_BROADLEAF"));
  CHECK(land(25, 40, 85).biome == id_of("SAVANNA_TROPICAL"));
  // Salinity: brackish wetlands count as fresh; saline tropical swamp with D <= 9 is mangrove.
  CHECK(land(50, 10, 50, 33).biome == id_of("MARSH_TEMPERATE_FRESHWATER"));
  CHECK(land(50, 10, 50, 66).biome == id_of("MARSH_TEMPERATE_SALTWATER"));
  CHECK(land(80, 9, 90, 66).biome == id_of("SWAMP_MANGROVE"));
  CHECK(land(80, 10, 90, 66).biome == id_of("SWAMP_TROPICAL_SALTWATER"));
  CHECK(land(80, 9, 90, 33).biome == id_of("SWAMP_TROPICAL_FRESHWATER"));
  CHECK(land(80, 9, 50, 66).biome == id_of("SWAMP_TEMPERATE_SALTWATER"));
  // Salinity does not change dry biomes.
  CHECK(land(70, 50, 50, 100).biome == id_of("FOREST_TEMPERATE_CONIFER"));
}

TEST_CASE("classify.BiomeClassifier takes ocean and lakes from water, rivers stay land") {
  const auto with_water = [](WaterClass w, double z, std::uint8_t s, double t = 50.0) {
    return classifier().classify({.elevation = static_cast<float>(z),
                                  .rainfall = 70.0F,
                                  .drainage = 50.0F,
                                  .temperature = static_cast<float>(t),
                                  .salinity = s,
                                  .water = u8(w),
                                  .has_water = true});
  };
  CHECK(with_water(WaterClass::ocean, 80, 100).biome == id_of("OCEAN_TEMPERATE"));
  CHECK(with_water(WaterClass::lake, 150, 0).biome == id_of("LAKE_TEMPERATE_FRESHWATER"));
  CHECK(with_water(WaterClass::lake, 150, 33).biome == id_of("LAKE_TEMPERATE_BRACKISHWATER"));
  CHECK(with_water(WaterClass::lake, 150, 100).biome == id_of("LAKE_TEMPERATE_SALTWATER"));
  CHECK(with_water(WaterClass::lake, 350, 66, 90).biome == id_of("LAKE_TROPICAL_SALTWATER"));
  CHECK(with_water(WaterClass::lake, 150, 0).region_type == type_of("lake"));
  CHECK(with_water(WaterClass::river, 150, 0).biome == id_of("FOREST_TEMPERATE_CONIFER"));
  CHECK(with_water(WaterClass::land, 150, 0).biome == id_of("FOREST_TEMPERATE_CONIFER"));
  // With a water field, a land cell below sea level is not ocean.
  CHECK(with_water(WaterClass::land, 99, 0).biome == id_of("FOREST_TEMPERATE_CONIFER"));
  CHECK(classifier().river_biome(50, 0) == id_of("RIVER_TEMPERATE_FRESHWATER"));
  CHECK(classifier().river_biome(90, 33) == id_of("RIVER_TROPICAL_BRACKISHWATER"));
  CHECK(classifier().river_biome(-30, 100) == id_of("RIVER_TEMPERATE_SALTWATER"));
}

TEST_CASE("classify.BiomeClassifier maps every threshold +-1 to the table's biome (A20)") {
  const BiomeTable& t = table();
  std::vector<std::int32_t> p_bounds;
  std::vector<std::int32_t> d_bounds = {t.thresholds.glacier_min_drainage,
                                        t.thresholds.mangrove_max_drainage};
  for (const auto& row : t.base) {
    p_bounds.insert(p_bounds.end(), {row.rainfall_min, row.rainfall_max});
    d_bounds.insert(d_bounds.end(), {row.drainage_min, row.drainage_max});
  }
  std::vector<std::int32_t> t_bounds;
  for (const auto& band : t.temperature_bands) {
    t_bounds.insert(t_bounds.end(), {band.min, band.max});
  }
  const std::vector<double> ps = edges_of(p_bounds);
  const std::vector<double> ds = edges_of(d_bounds);
  const std::vector<double> ts = edges_of(t_bounds);
  const std::vector<double> zs =
      edges_of({t.thresholds.ocean_max_elevation, t.thresholds.mountain_min_elevation, 200});
  std::int64_t checked = 0;
  const auto expect_reference = [&](const CellInputs& in) {
    const CellClass got = classifier().classify(in);
    const CellClass want = reference(in);
    if (got != want) {
      CAPTURE(in.rainfall);
      CAPTURE(in.drainage);
      CAPTURE(in.temperature);
      CAPTURE(in.elevation);
      CAPTURE(static_cast<int>(in.salinity));
      CAPTURE(static_cast<int>(in.water));
      CAPTURE(in.has_water);
      CHECK(got.biome == want.biome);
      CHECK(got.region_type == want.region_type);
    }
    ++checked;
  };
  // Land: rainfall, drainage, temperature and saline bounds.
  for (const double p : ps) {
    for (const double d : ds) {
      for (const double temp : ts) {
        for (const std::uint8_t s : {std::uint8_t{0}, std::uint8_t{65}, std::uint8_t{66}}) {
          expect_reference({.elevation = 150.0F,
                            .rainfall = static_cast<float>(p),
                            .drainage = static_cast<float>(d),
                            .temperature = static_cast<float>(temp),
                            .salinity = s,
                            .water = 0,
                            .has_water = false});
        }
      }
    }
  }
  // Elevation, water and salinity classes at a few rainfall and drainage values.
  for (const double z : zs) {
    for (const double temp : ts) {
      for (const std::uint8_t s : std::array<std::uint8_t, 7>{0, 32, 33, 65, 66, 99, 100}) {
        for (const std::uint8_t w : {u8(WaterClass::land), u8(WaterClass::ocean),
                                     u8(WaterClass::lake), u8(WaterClass::river)}) {
          for (const bool has_water : {false, true}) {
            for (const double pd : {5.0, 50.0, 80.0}) {
              expect_reference({.elevation = static_cast<float>(z),
                                .rainfall = static_cast<float>(pd),
                                .drainage = static_cast<float>(100.0 - pd),
                                .temperature = static_cast<float>(temp),
                                .salinity = s,
                                .water = w,
                                .has_water = has_water});
            }
          }
        }
      }
    }
  }
  CHECK(checked > 100000);
}

TEST_CASE("classify.BiomeClassifier matches the table for every integer rainfall and drainage") {
  for (const auto& band : table().temperature_bands) {
    for (std::int32_t p = 0; p <= 100; ++p) {
      for (std::int32_t d = 0; d <= 100; ++d) {
        for (const std::uint8_t s : {std::uint8_t{0}, std::uint8_t{66}}) {
          const CellInputs in{.elevation = 150.0F,
                              .rainfall = static_cast<float>(p),
                              .drainage = static_cast<float>(d),
                              .temperature = static_cast<float>(std::max(band.min, -1000)),
                              .salinity = s,
                              .water = 0,
                              .has_water = false};
          REQUIRE(classifier().classify(in) == reference(in));
        }
      }
    }
  }
}

TEST_CASE("classify.BiomeClassifier rounds down and clamps its inputs") {
  CHECK(land(9.99, 10, 50).biome == id_of("DESERT_SAND"));
  CHECK(land(10.0, 10, 50).biome == id_of("GRASSLAND_TEMPERATE"));
  CHECK(land(-5, 10, 50).biome == id_of("DESERT_SAND"));
  CHECK(land(150, 10, 50).biome == id_of("SWAMP_TEMPERATE_FRESHWATER"));
  CHECK(land(70, 50, 9.99).biome == id_of("FOREST_TAIGA"));
  CHECK(land(70, 50, -4.5).biome == id_of("TUNDRA"));
  CHECK(land(70, 50, 5000).biome == id_of("FOREST_TROPICAL_CONIFER"));
  CHECK(land(70, 50, std::nan("")).biome == id_of("TUNDRA"));
  CHECK(classifier().band_of(-4.5) == 0);
  CHECK(classifier().band_of(-4.0) == 1);
}

TEST_CASE("classify.BiomeClassifier::create rejects incomplete tables") {
  {
    BiomeTable t = table();
    t.temperature_bands[1].min = -3;  // gap at -4
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.base[0].rainfall_max = 10;  // overlaps the next row
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.base.pop_back();  // leaves rainfall 76-100 uncovered
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.variants.pop_back();
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.ocean.pop_back();
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.lakes.erase(t.lakes.begin());  // no fresh lake in the cold bands
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.region_types.erase(std::find(t.region_types.begin(), t.region_types.end(), "hills"));
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  {
    BiomeTable t = table();
    t.mangrove.band = "arctic";
    CHECK(BiomeClassifier::create(t).error().code == StatusCode::invalid_argument);
  }
  CHECK(classifier().region_type_count() == table().region_types.size());
}

TEST_CASE("classify.classify_biomes is the per-cell function on every cell") {
  const wg::core::Grid grid = wg::test::make_grid(33);
  const auto scaled = [&](std::uint64_t seed, double lo, double hi) {
    std::vector<float> v = wg::test::white_noise(grid, seed);
    for (float& x : v) {
      x = static_cast<float>(lo + ((hi - lo) * (static_cast<double>(x) + 0.5)));
    }
    return v;
  };
  const std::vector<float> z = scaled(1, 0, 400);
  const std::vector<float> p = scaled(2, 0, 100);
  const std::vector<float> d = scaled(3, 0, 100);
  const std::vector<float> t = scaled(4, -60, 120);
  std::vector<std::uint8_t> s(z.size());
  std::vector<std::uint8_t> w(z.size());
  for (std::size_t k = 0; k < z.size(); ++k) {
    s[k] = std::array<std::uint8_t, 4>{0, 33, 66, 100}[k % 4];
    w[k] = static_cast<std::uint8_t>((k / 3) % 4);
  }
  const wg::classify::BiomeInputs in{
      .elevation = z, .rainfall = p, .drainage = d, .temperature = t, .salinity = s, .water = w};
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  std::vector<BiomeId> a(z.size());
  std::vector<std::uint8_t> ta(z.size());
  std::vector<BiomeId> b(z.size());
  REQUIRE(wg::classify::classify_biomes(grid, in, classifier(), a, ta, serial).ok());
  REQUIRE(wg::classify::classify_biomes(grid, in, classifier(), b, {}, **pool).ok());
  CHECK(a == b);
  for (std::size_t k = 0; k < z.size(); ++k) {
    const CellClass c = classifier().classify({.elevation = z[k],
                                               .rainfall = p[k],
                                               .drainage = d[k],
                                               .temperature = t[k],
                                               .salinity = s[k],
                                               .water = w[k],
                                               .has_water = true});
    REQUIRE(a[k] == c.biome);
    REQUIRE(ta[k] == c.region_type);
  }
  // A cell's class does not depend on the other cells.
  std::vector<float> z2 = scaled(9, 0, 400);
  const std::size_t probe = 500;
  z2[probe] = z[probe];
  std::vector<BiomeId> c(z.size());
  const wg::classify::BiomeInputs in2{
      .elevation = z2, .rainfall = p, .drainage = d, .temperature = t, .salinity = s, .water = w};
  REQUIRE(wg::classify::classify_biomes(grid, in2, classifier(), c, {}, serial).ok());
  CHECK(c[probe] == a[probe]);
  // Without salinity and water: fresh, and ocean from the elevation.
  REQUIRE(wg::classify::classify_biomes(grid,
                                        {.elevation = z,
                                         .rainfall = p,
                                         .drainage = d,
                                         .temperature = t,
                                         .salinity = {},
                                         .water = {}},
                                        classifier(), c, {}, serial)
              .ok());
  for (std::size_t k = 0; k < z.size(); ++k) {
    REQUIRE(c[k] == classifier()
                        .classify({.elevation = z[k],
                                   .rainfall = p[k],
                                   .drainage = d[k],
                                   .temperature = t[k],
                                   .salinity = 0,
                                   .water = 0,
                                   .has_water = false})
                        .biome);
  }
}

TEST_CASE("classify.classify_biomes rejects bad arguments and honours cancel") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<float> f(static_cast<std::size_t>(grid.cells()), 50.0F);
  const std::vector<float> short_field(10, 50.0F);
  std::vector<BiomeId> out(f.size());
  std::vector<std::uint8_t> types(3);
  wg::core::SerialExecutor serial;
  const wg::classify::BiomeInputs good{
      .elevation = f, .rainfall = f, .drainage = f, .temperature = f, .salinity = {}, .water = {}};
  wg::classify::BiomeInputs bad = good;
  bad.rainfall = short_field;
  CHECK(wg::classify::classify_biomes(grid, bad, classifier(), out, {}, serial).code ==
        StatusCode::invalid_argument);
  CHECK(wg::classify::classify_biomes(grid, good, classifier(), out, types, serial).code ==
        StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::classify::classify_biomes(grid, good, classifier(), out, {}, serial, &cancel).code ==
        StatusCode::cancelled);
}
