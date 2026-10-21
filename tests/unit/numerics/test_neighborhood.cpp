#include "wg/numerics/neighborhood.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"

using wg::core::Dir8;
using wg::num::Connectivity;

static_assert(wg::num::directions(Connectivity::d4).size() == 4);
static_assert(wg::num::directions(Connectivity::d8).size() == 8);
static_assert(wg::num::step_length(Dir8::e) == 1.0);
static_assert(wg::num::step_length(Dir8::ne) == wg::num::kSqrt2);

namespace {

std::vector<Dir8> visited(const wg::core::Grid& grid, std::int64_t i, Connectivity c) {
  std::vector<Dir8> dirs;
  wg::num::for_each_neighbor(grid, i, c, [&](std::int64_t j, Dir8 d) {
    CHECK(j == grid.neighbor(i, d));
    dirs.push_back(d);
  });
  return dirs;
}

}  // namespace

TEST_CASE("numerics.neighborhood directions follow D8 order") {
  const auto d4 = wg::num::directions(Connectivity::d4);
  const auto d8 = wg::num::directions(Connectivity::d8);
  CHECK(std::vector<Dir8>(d4.begin(), d4.end()) ==
        std::vector<Dir8>{Dir8::e, Dir8::s, Dir8::w, Dir8::n});
  CHECK(std::vector<Dir8>(d8.begin(), d8.end()) ==
        std::vector<Dir8>(wg::core::kDir8.begin(), wg::core::kDir8.end()));
  CHECK(wg::num::kSqrt2 * wg::num::kSqrt2 == doctest::Approx(2.0).epsilon(1e-15));
}

TEST_CASE("numerics.neighborhood visits in-grid neighbours only") {
  const wg::core::Grid grid = wg::test::make_grid(5);
  const std::int64_t corner = grid.idx(0, 0);
  const std::int64_t edge = grid.idx(2, 4);
  const std::int64_t inner = grid.idx(2, 2);
  CHECK(visited(grid, corner, Connectivity::d8) == std::vector<Dir8>{Dir8::e, Dir8::se, Dir8::s});
  CHECK(visited(grid, corner, Connectivity::d4) == std::vector<Dir8>{Dir8::e, Dir8::s});
  CHECK(visited(grid, edge, Connectivity::d8) ==
        std::vector<Dir8>{Dir8::e, Dir8::w, Dir8::nw, Dir8::n, Dir8::ne});
  CHECK(visited(grid, edge, Connectivity::d4) == std::vector<Dir8>{Dir8::e, Dir8::w, Dir8::n});
  CHECK(visited(grid, inner, Connectivity::d8).size() == 8);
  CHECK(visited(grid, inner, Connectivity::d4).size() == 4);
}
