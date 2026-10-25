#include "wg/geology/minerals.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "support/stage_run.hpp"
#include "support/stats.hpp"

using wg::core::GeoColumn;
using wg::core::InclusionType;
using wg::core::MineralDeposit;
using wg::core::RngKey;
using wg::core::StatusCode;
using wg::geology::expected_deposits;
using wg::geology::GeoRegions;
using wg::geology::HostEntry;
using wg::geology::MineralHosts;
using wg::geology::place_minerals;

namespace {

constexpr RngKey kKey{0x0DE905175ULL};

const MineralHosts& hosts() {
  static const MineralHosts h = [] {
    auto created =
        MineralHosts::create(wg::test::repo_tables().minerals, wg::test::repo_tables().rocks);
    REQUIRE(created);
    return std::move(*created);
  }();
  return h;
}

std::uint16_t rock(std::string_view name) {
  const auto& rocks = wg::test::repo_tables().rocks;
  const auto* r = rocks.find(name);
  REQUIRE(r != nullptr);
  return static_cast<std::uint16_t>(r - rocks.rocks.data());
}

std::uint16_t mineral(std::string_view name) {
  const auto& minerals = wg::test::repo_tables().minerals.minerals;
  for (std::size_t i = 0; i < minerals.size(); ++i) {
    if (minerals[i].name == name) {
      return static_cast<std::uint16_t>(i);
    }
  }
  FAIL("no mineral " << name);
  return 0;
}

// count regions of area cells each, with consecutive cell numbers.
GeoRegions square_regions(std::size_t count, std::int64_t area) {
  GeoRegions g;
  g.offsets.push_back(0);
  for (std::size_t r = 0; r < count; ++r) {
    g.regions.push_back({.id = static_cast<std::int32_t>(r), .area_cells = area});
    for (std::int64_t c = 0; c < area; ++c) {
      g.cells.push_back((static_cast<std::int64_t>(r) * area) + c);
    }
    g.offsets.push_back(g.offsets.back() + area);
  }
  return g;
}

// One column per region with the given stone layers (rock, thickness in metres).
std::vector<GeoColumn> columns(std::size_t count,
                               const std::vector<std::pair<std::uint16_t, float>>& layers) {
  std::vector<GeoColumn> out(count);
  for (std::size_t r = 0; r < count; ++r) {
    out[r].geo_region = static_cast<std::int32_t>(r);
    for (const auto& [id, t] : layers) {
      out[r].layers.push_back({.rock_id = id, .thickness_m = t});
    }
  }
  return out;
}

}  // namespace

TEST_CASE("geology.MineralHosts indexes the environments by host") {
  const auto shale = hosts().in_rock(rock("shale"));
  // Sedimentary class minerals plus the coal named for shale.
  bool coal = false;
  for (const HostEntry& e : shale) {
    coal = coal || e.mineral == mineral("bituminous_coal");
  }
  CHECK(coal);
  const auto granite = hosts().in_rock(rock("granite"));
  for (const HostEntry& e : granite) {
    CHECK(e.mineral != mineral("bituminous_coal"));
  }
  const auto kimberlite = hosts().in_mineral(mineral("kimberlite"));
  REQUIRE(kimberlite.size() == 1);
  CHECK(kimberlite[0] == HostEntry{.mineral = mineral("diamond"),
                                   .inclusion = InclusionType::cluster_one,
                                   .frequency = 50});
  CHECK(hosts().in_rock(9999).empty());

  wg::params::MineralTable bad = wg::test::repo_tables().minerals;
  bad.minerals[0].environments[0].host_kind = wg::params::HostKind::rock;
  bad.minerals[0].environments[0].host = "moonstone";
  auto refused = MineralHosts::create(bad, wg::test::repo_tables().rocks);
  REQUIRE_FALSE(refused);
  CHECK(refused.error().code == StatusCode::invalid_argument);
}

TEST_CASE("geology.expected_deposits follows the Poisson intensity of R10.4") {
  const std::uint16_t gabbro = rock("gabbro");
  const double total = hosts().rock_frequency(gabbro);
  REQUIRE(total > 0.0);
  const auto entries = hosts().in_rock(gabbro);
  for (std::size_t e = 0; e < entries.size(); ++e) {
    const double expected =
        static_cast<double>(entries[e].frequency) / total * 1000.0 * 20.0 / 2500.0;
    CHECK(expected_deposits(hosts(), gabbro, e, 1000, 20.0, 2500) == doctest::Approx(expected));
  }
  CHECK(expected_deposits(hosts(), gabbro, 0, 1000, 20.0, 5000) ==
        doctest::Approx(expected_deposits(hosts(), gabbro, 0, 1000, 20.0, 2500) / 2.0));
  CHECK(expected_deposits(hosts(), gabbro, entries.size(), 1000, 20.0, 2500) == 0.0);
  CHECK(expected_deposits(hosts(), gabbro, 0, 1000, 20.0, 0) == 0.0);
}

TEST_CASE("geology.place_minerals counts lie in the 99 % Poisson interval of E[N_k]") {
  const std::size_t regions = 400;
  const std::int64_t area = 50;
  const double hu = 2.0;
  const std::uint16_t limestone = rock("limestone");
  const std::uint16_t gneiss = rock("gneiss");
  const std::uint16_t gabbro = rock("gabbro");
  const std::vector<std::pair<std::uint16_t, float>> layers = {
      {limestone, 30.0F}, {gneiss, 50.0F}, {gabbro, 80.0F}};
  const GeoRegions g = square_regions(regions, area);
  const std::vector<GeoColumn> cols = columns(regions, layers);
  const std::int32_t scarcity = 500;
  auto deposits = place_minerals(hosts(), cols, g, hu, scarcity, kKey);
  REQUIRE(deposits);

  const std::size_t minerals = wg::test::repo_tables().minerals.minerals.size();
  std::vector<double> expected(minerals, 0.0);
  for (const auto& [id, t] : layers) {
    const auto entries = hosts().in_rock(id);
    for (std::size_t e = 0; e < entries.size(); ++e) {
      expected[entries[e].mineral] +=
          static_cast<double>(regions) *
          expected_deposits(hosts(), id, e, area, static_cast<double>(t) / hu, scarcity);
    }
  }
  std::vector<double> observed(minerals, 0.0);
  for (const MineralDeposit& d : *deposits) {
    observed[d.mineral_id] += 1.0;
    REQUIRE(d.host_layer < layers.size());
    CHECK(d.cell >= 0);
    CHECK(d.cell < static_cast<std::int64_t>(regions) * area);
  }
  const std::uint16_t diamond = mineral("diamond");
  const std::uint16_t kimberlite = mineral("kimberlite");
  for (std::size_t m = 0; m < minerals; ++m) {
    if (m == diamond) {
      continue;  // nested; checked below
    }
    CAPTURE(wg::test::repo_tables().minerals.minerals[m].name);
    if (expected[m] == 0.0) {
      CHECK(observed[m] == 0.0);
    } else {
      CHECK(wg::test::poisson_interval99(expected[m]).contains(observed[m]));
    }
  }
  // Diamonds: Poisson(0.5) per kimberlite deposit, on the kimberlite's cell and layer.
  REQUIRE(observed[kimberlite] > 0.0);
  CHECK(wg::test::poisson_interval99(observed[kimberlite] * 0.5).contains(observed[diamond]));
  for (std::size_t i = 0; i < deposits->size(); ++i) {
    const MineralDeposit& d = (*deposits)[i];
    if (d.mineral_id == diamond) {
      REQUIRE(i > 0);
      std::size_t h = i - 1;
      while ((*deposits)[h].mineral_id == diamond) {
        --h;
      }
      CHECK((*deposits)[h].mineral_id == kimberlite);
      CHECK((*deposits)[h].cell == d.cell);
      CHECK((*deposits)[h].host_layer == d.host_layer);
    }
  }
}

TEST_CASE("geology.place_minerals keeps deposits in their region and host layer") {
  const GeoRegions g = square_regions(30, 40);
  const std::vector<GeoColumn> cols =
      columns(30, {{rock("sandstone"), 40.0F}, {rock("granite"), 60.0F}});
  auto a = place_minerals(hosts(), cols, g, 1.0, 100, kKey);
  REQUIRE(a);
  REQUIRE_FALSE(a->empty());
  for (const MineralDeposit& d : *a) {
    const auto region = static_cast<std::size_t>(d.cell / 40);
    REQUIRE(region < 30);
    const auto& host = cols[region].layers[d.host_layer];
    bool listed = false;
    for (const HostEntry& e : hosts().in_rock(host.rock_id)) {
      listed = listed || e.mineral == d.mineral_id;
    }
    CHECK(listed);
  }
  auto b = place_minerals(hosts(), cols, g, 1.0, 100, kKey);
  REQUIRE(b);
  CHECK(*a == *b);
  auto rarer = place_minerals(hosts(), cols, g, 1.0, 100000, kKey);
  REQUIRE(rarer);
  CHECK(rarer->size() < a->size());

  std::vector<GeoColumn> shuffled = cols;
  std::swap(shuffled[0], shuffled[1]);
  auto refused = place_minerals(hosts(), shuffled, g, 1.0, 100, kKey);
  REQUIRE_FALSE(refused);
  CHECK(refused.error().code == StatusCode::invalid_argument);
  CHECK_FALSE(place_minerals(hosts(), cols, g, 0.0, 100, kKey));
  CHECK_FALSE(
      place_minerals(hosts(), std::span<const GeoColumn>(cols).first(29), g, 1.0, 100, kKey));
}
