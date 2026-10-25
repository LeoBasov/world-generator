#include "wg/geology/column.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

#include "support/stage_run.hpp"
#include "support/stats.hpp"

using wg::core::GeoColumn;
using wg::core::RngKey;
using wg::core::StatusCode;
using wg::geology::check_column;
using wg::geology::ColumnInputs;
using wg::geology::draw_column;
using wg::geology::GeoEnvironment;
using wg::geology::GeologyTables;
using wg::params::GeologyParams;
using wg::params::RockClass;

namespace {

constexpr RngKey kKey{0x5EED0A21ULL};

const GeologyTables& tables() {
  static const GeologyTables t = [] {
    auto created =
        GeologyTables::create(wg::test::repo_tables().rocks, wg::test::repo_tables().soils);
    REQUIRE(created);
    return std::move(*created);
  }();
  return t;
}

GeoColumn draw(const ColumnInputs& in, const GeologyParams& p = GeologyParams{}) {
  auto column = draw_column(tables(), p, in, kKey);
  REQUIRE(column);
  return std::move(*column);
}

RockClass top_class(const GeoColumn& c) { return tables().rock_class(c.layers.front().rock_id); }

double stone_m(const GeoColumn& c) {
  double sum = 0.0;
  for (const auto& l : c.layers) {
    sum += static_cast<double>(l.thickness_m);
  }
  return sum;
}

double soil_m(const GeoColumn& c) {
  double sum = 0.0;
  for (const auto& l : c.soil_layers) {
    sum += static_cast<double>(l.thickness_m);
  }
  return sum;
}

constexpr std::size_t kColumns = 10000;

}  // namespace

TEST_CASE("geology.draw_column keeps the stratigraphic invariants in 10^4 columns") {
  const GeologyParams p;
  std::size_t extrusive_tops = 0;
  for (std::size_t i = 0; i < kColumns; ++i) {
    CAPTURE(i);
    const ColumnInputs in{.region = static_cast<std::int32_t>(i),
                          .environment = static_cast<GeoEnvironment>(i % 5),
                          .volcanism = static_cast<double>((i * 37) % 101),
                          .volcano = i % 97 == 0,
                          .soil_depth_m = static_cast<double>(i % 11),
                          .hu_m = 3.0};
    const GeoColumn c = draw(in, p);
    REQUIRE(check_column(c, tables()).ok());
    CHECK(c.geo_region == in.region);
    CHECK(c.layers.size() >= static_cast<std::size_t>(p.layer_count_min));
    CHECK(c.layers.size() <= static_cast<std::size_t>(p.layer_count_max));
    if (in.volcano) {
      CHECK(top_class(c) == RockClass::igneous_extrusive);
    }
    extrusive_tops += top_class(c) == RockClass::igneous_extrusive ? 1 : 0;
  }
  CHECK(extrusive_tops > 0);
}

TEST_CASE("geology.draw_column draws an extrusive top with probability V / 100") {
  for (const double v : {30.0, 70.0}) {
    CAPTURE(v);
    std::size_t count = 0;
    for (std::size_t i = 0; i < kColumns; ++i) {
      const GeoColumn c = draw({.region = static_cast<std::int32_t>(i),
                                .environment = GeoEnvironment::land,
                                .volcanism = v});
      count += top_class(c) == RockClass::igneous_extrusive ? 1 : 0;
    }
    CHECK(wg::test::binomial_interval99(kColumns, v / 100.0).contains(static_cast<double>(count)));
  }
  for (std::int32_t r = 0; r < 500; ++r) {
    const GeoColumn none =
        draw({.region = r, .environment = GeoEnvironment::land, .volcanism = 0.0});
    CHECK(top_class(none) != RockClass::igneous_extrusive);
    const GeoColumn all =
        draw({.region = r, .environment = GeoEnvironment::land, .volcanism = 100.0});
    CHECK(top_class(all) == RockClass::igneous_extrusive);
  }
}

TEST_CASE("geology.draw_column draws the sedimentary top share and log-normal thicknesses") {
  GeologyParams p;
  std::size_t sedimentary = 0;
  std::vector<double> log_thickness;
  for (std::size_t i = 0; i < kColumns; ++i) {
    const GeoColumn c = draw({.region = static_cast<std::int32_t>(i),
                              .environment = GeoEnvironment::land,
                              .volcanism = 0.0,
                              .hu_m = 2.0},
                             p);
    sedimentary += top_class(c) == RockClass::sedimentary ? 1 : 0;
    log_thickness.push_back(std::log(static_cast<double>(c.layers.front().thickness_m) / 2.0));
  }
  CHECK(wg::test::binomial_interval99(kColumns, p.sedimentary_top_probability)
            .contains(static_cast<double>(sedimentary)));
  const double mu = p.layer_thickness_log_mean;
  const double sigma = p.layer_thickness_log_sigma;
  const double d = wg::test::ks_statistic(log_thickness, [&](double x) {
    return 0.5 * std::erfc(-(x - mu) / (sigma * std::numbers::sqrt2));
  });
  CHECK(d < wg::test::ks_critical(kColumns));
}

TEST_CASE("geology.draw_column limits sedimentary rocks under oceans to their flags") {
  const auto& rocks = wg::test::repo_tables().rocks.rocks;
  for (const GeoEnvironment e : {GeoEnvironment::deep_ocean, GeoEnvironment::shallow_ocean}) {
    for (std::int32_t r = 0; r < 2000; ++r) {
      const GeoColumn c = draw({.region = r, .environment = e, .volcanism = 0.0});
      for (const auto& l : c.layers) {
        const auto& rock = rocks[l.rock_id];
        if (rock.rock_class == RockClass::sedimentary) {
          CHECK((e == GeoEnvironment::deep_ocean ? rock.deep_ocean : rock.shallow_ocean));
        }
      }
    }
  }
  CHECK(tables().rocks(RockClass::sedimentary, GeoEnvironment::land).size() >
        tables().rocks(RockClass::sedimentary, GeoEnvironment::deep_ocean).size());
  CHECK(tables().rocks(RockClass::metamorphic, GeoEnvironment::deep_ocean).size() ==
        tables().rocks(RockClass::metamorphic, GeoEnvironment::land).size());
}

TEST_CASE("geology.draw_column shares the soil depth among eligible soils") {
  const auto& soils = wg::test::repo_tables().soils.soils;
  GeologyParams p;
  for (std::int32_t r = 0; r < 1000; ++r) {
    const GeoColumn land =
        draw({.region = r, .environment = GeoEnvironment::land, .soil_depth_m = 4.0}, p);
    REQUIRE_FALSE(land.soil_layers.empty());
    CHECK(land.soil_layers.size() <= static_cast<std::size_t>(p.soil_max_layers));
    CHECK(soil_m(land) == doctest::Approx(4.0).epsilon(1e-5));
    for (const auto& l : land.soil_layers) {
      CHECK(soils[l.soil_id].land);
    }
    const GeoColumn desert =
        draw({.region = r, .environment = GeoEnvironment::sand_desert, .soil_depth_m = 1.0}, p);
    for (const auto& l : desert.soil_layers) {
      CHECK(soils[l.soil_id].sand_desert);
    }
    const GeoColumn deep =
        draw({.region = r, .environment = GeoEnvironment::deep_ocean, .soil_depth_m = 1.0}, p);
    for (const auto& l : deep.soil_layers) {
      CHECK(soils[l.soil_id].deep_ocean);
    }
    CHECK(draw({.region = r, .environment = GeoEnvironment::mountain, .soil_depth_m = 3.0})
              .soil_layers.empty());
    CHECK(draw({.region = r, .environment = GeoEnvironment::land, .soil_depth_m = 0.0})
              .soil_layers.empty());
  }
  p.soil_max_layers = 0;
  CHECK(draw({.environment = GeoEnvironment::land, .soil_depth_m = 3.0}, p).soil_layers.empty());
}

TEST_CASE("geology.draw_column places caverns and the magma sea within the column") {
  GeologyParams p;
  p.cavern_layer_openness_min = {10, 20, 30};
  p.cavern_layer_openness_max = {10, 25, 30};
  p.cavern_layer_passage_density_min = {0, 5, 1};
  p.cavern_layer_passage_density_max = {0, 9, 100};
  p.cavern_layer_water_min = {0, 0, 50};
  p.cavern_layer_water_max = {100, 0, 60};
  for (std::int32_t r = 0; r < 500; ++r) {
    const GeoColumn c =
        draw({.region = r, .environment = GeoEnvironment::land, .soil_depth_m = 2.0}, p);
    REQUIRE(c.caverns.size() == 2);  // level 0 has no passages
    CHECK(c.caverns[0].level == 1);
    CHECK(c.caverns[0].openness >= 20);
    CHECK(c.caverns[0].openness <= 25);
    CHECK(c.caverns[0].water == 0);
    CHECK(c.caverns[1].level == 2);
    CHECK(c.caverns[1].openness == 30);
    CHECK(c.caverns[1].water >= 50);
    CHECK(c.caverns[1].water <= 60);
    const double soil = soil_m(c);
    const double stone = stone_m(c);
    CHECK(c.caverns[0].depth_m == doctest::Approx(soil + (stone * 0.5)));
    CHECK(c.caverns[1].depth_m == doctest::Approx(soil + (stone * 0.75)));
    CHECK(c.magma_depth_m == doctest::Approx(soil + stone));
  }
}

TEST_CASE("geology.draw_column makes the bottom layer thicker and is deterministic") {
  std::size_t thickest = 0;
  for (std::int32_t r = 0; r < 1000; ++r) {
    const GeoColumn c = draw({.region = r, .environment = GeoEnvironment::land, .volcanism = 50.0});
    CHECK(c == draw({.region = r, .environment = GeoEnvironment::land, .volcanism = 50.0}));
    float max_other = 0.0F;
    for (std::size_t k = 0; k + 1 < c.layers.size(); ++k) {
      max_other = std::max(max_other, c.layers[k].thickness_m);
    }
    thickest += c.layers.back().thickness_m > max_other ? 1 : 0;
  }
  CHECK(thickest > 500);  // "usually thickest"
  CHECK(draw({.region = 1}) != draw({.region = 2}));
}

TEST_CASE("geology.draw_column refuses invalid parameters") {
  GeologyParams p;
  p.layer_count_min = 1;
  auto c = draw_column(tables(), p, {}, kKey);
  REQUIRE_FALSE(c);
  CHECK(c.error().code == StatusCode::invalid_argument);
  p = GeologyParams{};
  p.layer_count_max = 2;
  p.layer_count_min = 3;
  CHECK_FALSE(draw_column(tables(), p, {}, kKey));
  p = GeologyParams{};
  p.cavern_layer_water_min[1] = 80;
  p.cavern_layer_water_max[1] = 20;
  CHECK_FALSE(draw_column(tables(), p, {}, kKey));
  CHECK_FALSE(draw_column(tables(), GeologyParams{}, {.hu_m = 0.0}, kKey));
  p = GeologyParams{};
  p.layer_count_min = 2;
  p.layer_count_max = 2;
  const GeoColumn two = draw({.region = 4, .volcanism = 100.0}, p);
  CHECK(two.layers.size() == 2);
  CHECK(check_column(two, tables()).ok());
}

TEST_CASE("geology.check_column names the broken rule") {
  const auto& rocks = wg::test::repo_tables().rocks;
  auto id = [&](const char* name) {
    const auto* r = rocks.find(name);
    REQUIRE(r != nullptr);
    return static_cast<std::uint16_t>(r - rocks.rocks.data());
  };
  auto column = [&](std::initializer_list<const char*> names) {
    GeoColumn c;
    for (const char* n : names) {
      c.layers.push_back({.rock_id = id(n), .thickness_m = 10.0F});
    }
    c.magma_depth_m = 100.0F;
    return c;
  };
  CHECK(check_column(column({"sandstone", "schist", "granite"}), tables()).ok());
  CHECK(check_column(column({"basalt", "gneiss", "granite", "gabbro"}), tables()).ok());
  auto message = [&](const GeoColumn& c) { return check_column(c, tables()).message; };
  CHECK(message(column({"granite"})) == "check_column: fewer than two stone layers");
  CHECK(message(column({"sandstone", "basalt", "granite"})) ==
        "check_column: sedimentary and extrusive layers in one column");
  CHECK(message(column({"schist", "sandstone", "granite"})) ==
        "check_column: metamorphic layer above a sedimentary layer");
  CHECK(message(column({"granite", "schist", "granite"})) == "check_column: intrusive top layer");
  CHECK(message(column({"sandstone", "schist"})) == "check_column: bottom layer not intrusive");
  GeoColumn c = column({"sandstone", "granite"});
  c.layers[0].thickness_m = 0.0F;
  CHECK(message(c) == "check_column: stone thickness not positive");
  c = column({"sandstone", "granite"});
  c.caverns = {{.level = 2, .depth_m = 50.0F}, {.level = 1, .depth_m = 60.0F}};
  CHECK(message(c) == "check_column: cavern levels not ascending");
  c.caverns = {{.level = 0, .depth_m = 150.0F}};
  CHECK(message(c) == "check_column: cavern outside the stone");
}

TEST_CASE("geology.GeologyTables needs a rock in every class") {
  wg::params::RockTable rocks = wg::test::repo_tables().rocks;
  std::erase_if(rocks.rocks, [](const auto& r) { return r.rock_class == RockClass::metamorphic; });
  auto t = GeologyTables::create(rocks, wg::test::repo_tables().soils);
  REQUIRE_FALSE(t);
  CHECK(t.error().code == StatusCode::invalid_argument);
  CHECK(tables().soils(GeoEnvironment::mountain).empty());
  CHECK_FALSE(tables().soils(GeoEnvironment::land).empty());
}
