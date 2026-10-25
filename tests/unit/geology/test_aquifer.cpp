#include "wg/geology/aquifer.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "support/stats.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::GeoColumn;
using wg::core::Grid;
using wg::core::RngKey;
using wg::core::StatusCode;
using wg::geology::aquifer_probability;
using wg::geology::AquiferClass;
using wg::geology::AquiferInputs;
using wg::geology::assign_aquifers;
using wg::geology::column_has_aquifer;
using wg::geology::GeoEnvironment;
using wg::geology::GeologyTables;

namespace {

constexpr RngKey kKey{0xA9F1ULL};

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

std::uint8_t env(GeoEnvironment e) { return static_cast<std::uint8_t>(e); }

std::uint8_t cls(AquiferClass a) { return static_cast<std::uint8_t>(a); }

const GeologyTables& tables() {
  static const GeologyTables t = [] {
    auto created =
        GeologyTables::create(wg::test::repo_tables().rocks, wg::test::repo_tables().soils);
    REQUIRE(created);
    return std::move(*created);
  }();
  return t;
}

std::uint16_t rock(const char* name) {
  const auto& rocks = wg::test::repo_tables().rocks;
  const auto* r = rocks.find(name);
  REQUIRE(r != nullptr);
  return static_cast<std::uint16_t>(r - rocks.rocks.data());
}

std::uint16_t soil(const char* name) {
  const auto& soils = wg::test::repo_tables().soils.soils;
  for (std::size_t i = 0; i < soils.size(); ++i) {
    if (soils[i].name == name) {
      return static_cast<std::uint16_t>(i);
    }
  }
  FAIL("no soil " << name);
  return 0;
}

struct Inputs {
  std::vector<float> z;
  std::vector<float> d;
  std::vector<std::uint8_t> e;
  std::vector<std::int32_t> region;
  std::vector<std::uint8_t> has;

  [[nodiscard]] AquiferInputs view() const {
    return {.elevation = z,
            .drainage = d,
            .environment = e,
            .geo_region = region,
            .region_has_aquifer = has};
  }
};

// Land at elevation z and drainage d everywhere, one region with an aquifer layer.
Inputs uniform(const Grid& grid, float z, float d) {
  const auto n = u(grid.cells());
  return {std::vector<float>(n, z), std::vector<float>(n, d),
          std::vector<std::uint8_t>(n, env(GeoEnvironment::land)), std::vector<std::int32_t>(n, 0),
          std::vector<std::uint8_t>{1}};
}

std::size_t count(const std::vector<std::uint8_t>& a, AquiferClass c) {
  std::size_t k = 0;
  for (const std::uint8_t v : a) {
    k += v == cls(c) ? 1 : 0;
  }
  return k;
}

}  // namespace

TEST_CASE("geology.column_has_aquifer looks for an AQUIFER layer") {
  GeoColumn c;
  c.layers = {{.rock_id = rock("shale"), .thickness_m = 5.0F},
              {.rock_id = rock("granite"), .thickness_m = 5.0F}};
  c.soil_layers = {{.soil_id = soil("clay"), .thickness_m = 1.0F}};
  CHECK_FALSE(column_has_aquifer(c, tables()));
  c.layers[0].rock_id = rock("sandstone");
  CHECK(column_has_aquifer(c, tables()));
  c.layers[0].rock_id = rock("shale");
  c.soil_layers.push_back({.soil_id = soil("loam"), .thickness_m = 1.0F});
  CHECK(column_has_aquifer(c, tables()));
}

TEST_CASE("geology.aquifer_probability favours low ground and low drainage") {
  CHECK(aquifer_probability(100.0F, 0.0F) == 1.0);
  CHECK(aquifer_probability(100.0F, 100.0F) == 0.0);
  CHECK(aquifer_probability(300.0F, 0.0F) == 0.0);
  CHECK(aquifer_probability(200.0F, 50.0F) == doctest::Approx(0.25));
  CHECK(aquifer_probability(150.0F, 20.0F) > aquifer_probability(250.0F, 20.0F));
  CHECK(aquifer_probability(150.0F, -20.0F) == aquifer_probability(150.0F, 0.0F));
  CHECK(aquifer_probability(std::numeric_limits<float>::quiet_NaN(), 10.0F) == 0.0);
}

TEST_CASE("geology.assign_aquifers draws aquifers at the expected rate") {
  const Grid grid = wg::test::make_grid(129);
  const Inputs in = uniform(grid, 150.0F, 30.0F);  // p = 0.7 * 0.75
  std::vector<std::uint8_t> a(u(grid.cells()));
  wg::core::SerialExecutor exec;
  REQUIRE(assign_aquifers(in.view(), grid, false, kKey, a, exec).ok());
  const std::size_t n = u(grid.cells());
  const std::size_t wet = n - count(a, AquiferClass::none);
  CHECK(wg::test::binomial_interval99(n, 0.525).contains(static_cast<double>(wet)));
  const std::size_t heavy = count(a, AquiferClass::heavy);
  CHECK(wg::test::binomial_interval99(wet, wg::geology::kHeavyAquiferChance)
            .contains(static_cast<double>(heavy)));
  // The thread pool gives the same field.
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  std::vector<std::uint8_t> b(n);
  REQUIRE(assign_aquifers(in.view(), grid, false, kKey, b, **pool).ok());
  CHECK(a == b);
}

TEST_CASE("geology.assign_aquifers applies the DF heavy quirk") {
  const Grid grid = wg::test::make_grid(33);
  wg::core::SerialExecutor exec;
  std::vector<std::uint8_t> a(u(grid.cells()));
  REQUIRE(assign_aquifers(uniform(grid, 100.0F, 7.5F).view(), grid, true, kKey, a, exec).ok());
  CHECK(count(a, AquiferClass::light) == 0);
  CHECK(count(a, AquiferClass::heavy) > 0);
  REQUIRE(assign_aquifers(uniform(grid, 100.0F, 47.0F).view(), grid, true, kKey, a, exec).ok());
  CHECK(count(a, AquiferClass::light) == 0);
  REQUIRE(assign_aquifers(uniform(grid, 100.0F, 8.0F).view(), grid, true, kKey, a, exec).ok());
  CHECK(count(a, AquiferClass::heavy) == 0);
  CHECK(count(a, AquiferClass::light) > 0);
}

TEST_CASE("geology.assign_aquifers leaves oceans, mountains and dry columns without aquifers") {
  const Grid grid = wg::test::make_grid(17);
  wg::core::SerialExecutor exec;
  std::vector<std::uint8_t> a(u(grid.cells()));
  for (const GeoEnvironment e :
       {GeoEnvironment::deep_ocean, GeoEnvironment::shallow_ocean, GeoEnvironment::mountain}) {
    Inputs in = uniform(grid, 100.0F, 0.0F);
    in.e.assign(in.e.size(), env(e));
    REQUIRE(assign_aquifers(in.view(), grid, false, kKey, a, exec).ok());
    CHECK(count(a, AquiferClass::none) == a.size());
  }
  Inputs dry = uniform(grid, 100.0F, 0.0F);
  dry.has[0] = 0;
  REQUIRE(assign_aquifers(dry.view(), grid, false, kKey, a, exec).ok());
  CHECK(count(a, AquiferClass::none) == a.size());
  Inputs all = uniform(grid, 100.0F, 0.0F);  // p = 1
  REQUIRE(assign_aquifers(all.view(), grid, false, kKey, a, exec).ok());
  CHECK(count(a, AquiferClass::none) == 0);
  Inputs bad = uniform(grid, 100.0F, 0.0F);
  bad.region[3] = 1;
  CHECK(assign_aquifers(bad.view(), grid, false, kKey, a, exec).code ==
        StatusCode::invalid_argument);
}
