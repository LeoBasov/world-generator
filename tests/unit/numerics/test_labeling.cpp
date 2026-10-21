#include "wg/numerics/labeling.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"

using wg::core::StatusCode;
using wg::num::Connectivity;
using wg::num::UnionFind;

namespace {

// Independent reference: breadth-first flood fill started from every unlabelled mask cell in
// row-major order, so components are numbered by their first cell like label_components.
std::vector<std::int32_t> flood_fill(const wg::core::Grid& grid,
                                     const std::vector<std::uint8_t>& mask, Connectivity c,
                                     std::int32_t& count) {
  std::vector<std::int32_t> labels(mask.size(), 0);
  count = 0;
  for (std::size_t start = 0; start < mask.size(); ++start) {
    if (mask[start] == 0 || labels[start] != 0) {
      continue;
    }
    labels[start] = ++count;
    std::deque<std::int64_t> open{static_cast<std::int64_t>(start)};
    while (!open.empty()) {
      const std::int64_t i = open.front();
      open.pop_front();
      wg::num::for_each_neighbor(grid, i, c, [&](std::int64_t j, wg::core::Dir8 /*d*/) {
        const auto k = static_cast<std::size_t>(j);
        if (mask[k] != 0 && labels[k] == 0) {
          labels[k] = count;
          open.push_back(j);
        }
      });
    }
  }
  return labels;
}

}  // namespace

TEST_CASE("numerics.labeling equals flood fill on random masks") {
  for (const Connectivity c : {Connectivity::d4, Connectivity::d8}) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
      const wg::core::Grid grid = wg::test::make_grid(seed % 2 == 0 ? 33 : 65);
      const double density = 0.3 + (0.02 * static_cast<double>(seed));
      const std::vector<std::uint8_t> mask = wg::test::random_mask(grid, density, seed);
      CAPTURE(static_cast<int>(c));
      CAPTURE(seed);
      std::int32_t expected_count = 0;
      const std::vector<std::int32_t> expected = flood_fill(grid, mask, c, expected_count);
      std::vector<std::int32_t> labels(mask.size(), -1);
      const wg::core::Expected<std::int32_t> count =
          wg::num::label_components(mask, grid, c, labels);
      REQUIRE(count);
      CHECK(*count == expected_count);
      CHECK(labels == expected);
    }
  }
}

TEST_CASE("numerics.labeling joins diagonal cells only under D8") {
  const wg::core::Grid grid = wg::test::make_grid(5);
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 0);
  mask[static_cast<std::size_t>(grid.idx(1, 1))] = 1;
  mask[static_cast<std::size_t>(grid.idx(2, 2))] = 1;
  mask[static_cast<std::size_t>(grid.idx(3, 1))] = 1;
  std::vector<std::int32_t> labels(mask.size());
  CHECK(*wg::num::label_components(mask, grid, Connectivity::d4, labels) == 3);
  CHECK(*wg::num::label_components(mask, grid, Connectivity::d8, labels) == 1);

  const wg::core::Expected<std::vector<std::int64_t>> sizes = wg::num::component_sizes(labels, 1);
  REQUIRE(sizes);
  CHECK(*sizes == std::vector<std::int64_t>{22, 3});

  const std::vector<std::uint8_t> empty(mask.size(), 0);
  CHECK(*wg::num::label_components(empty, grid, Connectivity::d8, labels) == 0);
}

TEST_CASE("numerics.labeling union-find roots are the smallest elements") {
  CHECK(UnionFind::create(0).error().code == StatusCode::invalid_argument);
  auto sets = UnionFind::create(10);
  REQUIRE(sets);
  CHECK(sets->count() == 10);
  CHECK(sets->unite(7, 4) == 4);
  CHECK(sets->unite(9, 7) == 4);
  CHECK(sets->unite(2, 9) == 2);
  CHECK(sets->unite(4, 2) == 2);
  CHECK(sets->find(9) == 2);
  CHECK(sets->size_of(7) == 4);
  CHECK(sets->size_of(0) == 1);
  CHECK(sets->find(0) == 0);
}

TEST_CASE("numerics.labeling rejects bad arguments") {
  const wg::core::Grid grid = wg::test::make_grid(5);
  const std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 1);
  std::vector<std::int32_t> labels(mask.size());
  std::vector<std::int32_t> short_labels(3);
  CHECK(wg::num::label_components(mask, grid, Connectivity::d4, short_labels).error().code ==
        StatusCode::invalid_argument);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::label_components(mask, grid, Connectivity::d4, labels, resource).error().code ==
        StatusCode::out_of_memory);
  CHECK(wg::num::component_sizes(labels, -1).error().code == StatusCode::invalid_argument);
  labels[0] = 5;
  CHECK(wg::num::component_sizes(labels, 1).error().code == StatusCode::invalid_argument);
}
