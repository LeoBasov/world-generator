#include "wg/classify/regions.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <set>
#include <string_view>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/classify/biome.hpp"
#include "wg/core/executor.hpp"

using wg::classify::label_regions;
using wg::classify::RegionTypeCount;
using wg::core::Grid;
using wg::core::Region;
using wg::core::StatusCode;
using wg::params::BiomeId;
using wg::params::RegionSize;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

const std::vector<RegionSize> kSizes = {{"small", 1}, {"medium", 25}, {"large", 100}};

// Region types from rows of digits, row 0 first.
std::vector<std::uint8_t> pattern(const Grid& grid, const std::vector<std::string_view>& rows) {
  REQUIRE(rows.size() == static_cast<std::size_t>(grid.n));
  std::vector<std::uint8_t> types(u(grid.cells()));
  for (std::int32_t y = 0; y < grid.n; ++y) {
    REQUIRE(rows[u(y)].size() == static_cast<std::size_t>(grid.n));
    for (std::int32_t x = 0; x < grid.n; ++x) {
      types[u(grid.idx(x, y))] = static_cast<std::uint8_t>(rows[u(y)][u(x)] - '0');
    }
  }
  return types;
}

struct Labelled {
  std::vector<Region> regions;
  std::vector<std::int32_t> ids;
};

Labelled label(const Grid& grid, const std::vector<std::uint8_t>& types,
               const std::vector<BiomeId>& biomes = {}) {
  std::vector<BiomeId> b = biomes;
  if (b.empty()) {
    b.assign(types.begin(), types.end());
  }
  Labelled out;
  out.ids.assign(types.size(), -7);
  auto regions = label_regions(types, b, grid, kSizes, out.ids);
  REQUIRE(regions);
  out.regions = std::move(*regions);
  return out;
}

// Checks every invariant of label_regions on one labelling.
void check_invariants(const Grid& grid, const std::vector<std::uint8_t>& types,
                      const std::vector<BiomeId>& biomes, const Labelled& l) {
  const std::size_t cells = types.size();
  std::int64_t total = 0;
  std::vector<std::int64_t> first(l.regions.size(), -1);
  for (std::size_t i = 0; i < cells; ++i) {
    const std::int32_t id = l.ids[i];
    REQUIRE(id >= 0);
    REQUIRE(static_cast<std::size_t>(id) < l.regions.size());
    REQUIRE(l.regions[u(id)].type == types[i]);
    if (first[u(id)] < 0) {
      first[u(id)] = static_cast<std::int64_t>(i);
    }
  }
  // Ids follow the first cells in row-major order.
  CHECK(std::is_sorted(first.begin(), first.end()));
  for (std::size_t id = 0; id < l.regions.size(); ++id) {
    const Region& r = l.regions[id];
    CHECK(r.id == static_cast<std::int32_t>(id));
    CHECK(r.name_id == r.id);
    total += r.area_cells;
    // Connected: a D8 flood fill inside the region from its first cell reaches its whole area.
    std::vector<std::uint8_t> seen(cells, 0);
    std::queue<std::int64_t> open;
    open.push(first[id]);
    seen[u(first[id])] = 1;
    std::int64_t reached = 0;
    double sx = 0.0;
    double sy = 0.0;
    std::set<BiomeId> present;
    while (!open.empty()) {
      const std::int64_t i = open.front();
      open.pop();
      ++reached;
      sx += grid.x_of(i);
      sy += grid.y_of(i);
      present.insert(biomes[u(i)]);
      for (std::uint8_t d = 0; d < 8; ++d) {
        const std::int64_t j = grid.neighbor(i, static_cast<wg::core::Dir8>(d));
        if (j != wg::core::kNoCell && seen[u(j)] == 0 && l.ids[u(j)] == r.id) {
          seen[u(j)] = 1;
          open.push(j);
        }
      }
    }
    CHECK(reached == r.area_cells);
    CHECK(r.centroid_x == doctest::Approx(sx / static_cast<double>(reached)));
    CHECK(r.centroid_y == doctest::Approx(sy / static_cast<double>(reached)));
    CHECK(r.biomes == std::vector<BiomeId>(present.begin(), present.end()));
    CHECK(r.size_class == wg::classify::region_size_class(r.area_cells, kSizes));
  }
  CHECK(total == grid.cells());
  // Maximal: 8-neighbours of the same type share the region.
  for (std::size_t i = 0; i < cells; ++i) {
    for (std::uint8_t d = 0; d < 8; ++d) {
      const std::int64_t j =
          grid.neighbor(static_cast<std::int64_t>(i), static_cast<wg::core::Dir8>(d));
      if (j != wg::core::kNoCell && types[u(j)] == types[i]) {
        REQUIRE(l.ids[u(j)] == l.ids[i]);
      }
    }
  }
}

}  // namespace

TEST_CASE("classify.label_regions joins diagonal neighbours and splits separate areas") {
  const Grid grid = wg::test::make_grid(5);
  const std::vector<std::uint8_t> types = pattern(grid, {"12000",  //
                                                         "21000",  //
                                                         "00000",  //
                                                         "00330",  //
                                                         "30000"});
  const Labelled l = label(grid, types);
  // 1 (diagonal), 2 (crossing diagonal), 0, 3 (pair), 3 (corner).
  REQUIRE(l.regions.size() == 5);
  CHECK(l.ids[u(grid.idx(0, 0))] == 0);
  CHECK(l.ids[u(grid.idx(1, 1))] == 0);
  CHECK(l.ids[u(grid.idx(1, 0))] == 1);
  CHECK(l.ids[u(grid.idx(0, 1))] == 1);
  CHECK(l.ids[u(grid.idx(2, 0))] == 2);
  CHECK(l.ids[u(grid.idx(2, 3))] == 3);
  CHECK(l.ids[u(grid.idx(0, 4))] == 4);
  CHECK(l.regions[0].area_cells == 2);
  CHECK(l.regions[2].area_cells == 18);
  CHECK(l.regions[3].type == 3);
  CHECK(l.regions[3].centroid_x == 2.5);
  CHECK(l.regions[3].centroid_y == 3.0);
  check_invariants(grid, types, std::vector<BiomeId>(types.begin(), types.end()), l);
}

TEST_CASE("classify.label_regions keeps a hole inside a closed ring apart from the outside") {
  const Grid grid = wg::test::make_grid(5);
  const std::vector<std::uint8_t> types = pattern(grid, {"00000",  //
                                                         "01110",  //
                                                         "01010",  //
                                                         "01110",  //
                                                         "00000"});
  const Labelled l = label(grid, types);
  REQUIRE(l.regions.size() == 3);
  CHECK(l.regions[0].area_cells == 16);
  CHECK(l.regions[1].area_cells == 8);
  CHECK(l.regions[2].area_cells == 1);
  CHECK(l.regions[2].centroid_x == 2.0);
  // Outside and hole have the same centroid but are different regions.
  CHECK(l.regions[0].centroid_x == 2.0);
  CHECK(l.regions[0].centroid_y == 2.0);
}

TEST_CASE("classify.label_regions holds its invariants on random region types (A20)") {
  const Grid grid = wg::test::make_grid(33);
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    CAPTURE(seed);
    const std::vector<float> noise = wg::test::white_noise(grid, seed);
    const std::vector<float> fine = wg::test::white_noise(grid, seed + 100);
    std::vector<std::uint8_t> types(noise.size());
    std::vector<BiomeId> biomes(noise.size());
    for (std::size_t i = 0; i < noise.size(); ++i) {
      types[i] = static_cast<std::uint8_t>((static_cast<double>(noise[i]) + 0.5) * 3.0);
      biomes[i] = static_cast<BiomeId>(
          (10 * types[i]) + static_cast<int>((static_cast<double>(fine[i]) + 0.5) * 4.0));
    }
    const Labelled l = label(grid, types, biomes);
    check_invariants(grid, types, biomes, l);
    auto counts = wg::classify::count_region_types(types, l.regions, 3);
    REQUIRE(counts);
    std::int64_t cells = 0;
    std::int64_t regions = 0;
    for (const RegionTypeCount& c : *counts) {
      cells += c.cells;
      regions += c.regions;
    }
    CHECK(cells == grid.cells());
    CHECK(regions == static_cast<std::int64_t>(l.regions.size()));
  }
}

TEST_CASE("classify.label_regions: land regions sum to the land area of a classified map") {
  const Grid grid = wg::test::make_grid(65);
  const auto scaled = [&](std::uint64_t seed, double lo, double hi) {
    std::vector<float> v = wg::test::white_noise(grid, seed);
    for (float& x : v) {
      x = static_cast<float>(lo + ((hi - lo) * (static_cast<double>(x) + 0.5)));
    }
    return v;
  };
  const std::vector<float> z = scaled(11, 50, 350);
  const std::vector<float> p = scaled(12, 0, 100);
  const std::vector<float> d = scaled(13, 0, 100);
  const std::vector<float> t = scaled(14, -20, 100);
  const wg::params::BiomeTable& table = wg::test::repo_tables().biomes;
  auto classifier = wg::classify::BiomeClassifier::create(table);
  REQUIRE(classifier);
  std::vector<BiomeId> biome(z.size());
  std::vector<std::uint8_t> types(z.size());
  wg::core::SerialExecutor serial;
  REQUIRE(wg::classify::classify_biomes(grid,
                                        {.elevation = z,
                                         .rainfall = p,
                                         .drainage = d,
                                         .temperature = t,
                                         .salinity = {},
                                         .water = {}},
                                        *classifier, biome, types, serial)
              .ok());
  std::vector<std::int32_t> ids(z.size());
  auto regions = label_regions(types, biome, grid, table.region_sizes, ids);
  REQUIRE(regions);
  const auto ocean = static_cast<std::uint16_t>(
      std::find(table.region_types.begin(), table.region_types.end(), "ocean") -
      table.region_types.begin());
  std::int64_t land_area = 0;
  std::int64_t ocean_area = 0;
  for (const Region& r : *regions) {
    (r.type == ocean ? ocean_area : land_area) += r.area_cells;
  }
  const auto land_cells = std::count_if(z.begin(), z.end(), [](float v) { return v >= 100.0F; });
  CHECK(land_area == land_cells);
  CHECK(land_area + ocean_area == grid.cells());
  CHECK(regions->size() > 10);
}

TEST_CASE("classify.label_regions assigns size classes and calls the naming hook") {
  CHECK(wg::classify::region_size_class(1, kSizes) == 0);
  CHECK(wg::classify::region_size_class(24, kSizes) == 0);
  CHECK(wg::classify::region_size_class(25, kSizes) == 1);
  CHECK(wg::classify::region_size_class(99, kSizes) == 1);
  CHECK(wg::classify::region_size_class(100, kSizes) == 2);
  CHECK(wg::classify::region_size_class(5, {}) == 0);

  const Grid grid = wg::test::make_grid(17);
  std::vector<std::uint8_t> types(u(grid.cells()), 0);
  for (std::int32_t y = 0; y < 5; ++y) {
    for (std::int32_t x = 0; x < 5; ++x) {
      types[u(grid.idx(x, y))] = 1;  // 25 cells: medium
    }
  }
  types[u(grid.idx(16, 16))] = 2;  // 1 cell: small
  const std::vector<BiomeId> biomes(types.begin(), types.end());
  std::vector<std::int32_t> ids(types.size());
  std::int32_t calls = 0;
  const auto name = [&](const Region& r) {
    ++calls;
    return 100 + r.id;
  };
  const wg::classify::RegionNamer namer(name);
  auto regions = label_regions(types, biomes, grid, kSizes, ids, &namer);
  REQUIRE(regions);
  REQUIRE(regions->size() == 3);
  CHECK(calls == 3);
  CHECK((*regions)[0].size_class == 1);
  CHECK((*regions)[0].name_id == 100);
  CHECK((*regions)[1].size_class == 2);  // the 263 cells of type 0
  CHECK((*regions)[2].size_class == 0);
  CHECK((*regions)[2].name_id == 102);
}

TEST_CASE("classify.label_regions and count_region_types reject bad arguments") {
  const Grid grid = wg::test::make_grid(5);
  std::vector<std::uint8_t> types(u(grid.cells()), 0);
  std::vector<BiomeId> biomes(types.size(), 0);
  std::vector<std::int32_t> ids(types.size());
  std::vector<std::int32_t> short_ids(3);
  CHECK(label_regions(types, biomes, grid, kSizes, short_ids).error().code ==
        StatusCode::invalid_argument);
  CHECK(label_regions(types, std::vector<BiomeId>(2), grid, kSizes, ids).error().code ==
        StatusCode::invalid_argument);
  types[3] = 5;
  CHECK(wg::classify::count_region_types(types, {}, 4).error().code ==
        StatusCode::invalid_argument);
  Region bad_type;
  bad_type.type = 9;
  const std::vector<Region> regions = {bad_type};
  CHECK(wg::classify::count_region_types({}, regions, 4).error().code ==
        StatusCode::invalid_argument);
}
