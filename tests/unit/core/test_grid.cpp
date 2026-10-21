#include "wg/core/grid.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

using wg::core::Dir8;
using wg::core::Grid;
using wg::core::kDir4;
using wg::core::kDir8;
using wg::core::kNoCell;
using wg::core::StatusCode;

namespace {

constexpr Grid make_grid(std::int32_t n) { return {.n = n, .dx_m = 1000.0, .hu_m = 10.0}; }

static_assert(wg::core::opposite(Dir8::e) == Dir8::w);
static_assert(wg::core::opposite(Dir8::ne) == Dir8::sw);
static_assert(wg::core::is_diagonal(Dir8::se) && !wg::core::is_diagonal(Dir8::s));
static_assert(make_grid(5).idx(2, 3) == 17);
static_assert(make_grid(5).neighbor(0, Dir8::nw) == kNoCell);

}  // namespace

TEST_CASE("core.grid index round trip covers every cell") {
  for (const std::int32_t n : {3, 5, 33}) {
    const Grid g = make_grid(n);
    CHECK(g.cells() == std::int64_t{n} * n);
    for (std::int64_t i = 0; i < g.cells(); ++i) {
      const std::int32_t x = g.x_of(i);
      const std::int32_t y = g.y_of(i);
      REQUIRE(g.contains(x, y));
      REQUIRE(g.idx(x, y) == i);
    }
    CHECK_FALSE(g.contains(-1, 0));
    CHECK_FALSE(g.contains(0, n));
    CHECK_FALSE(g.contains(std::int64_t{-1}));
    CHECK_FALSE(g.contains(g.cells()));
  }
}

TEST_CASE("core.grid neighbour round trip including borders") {
  for (const std::int32_t n : {3, 5, 33}) {
    const Grid g = make_grid(n);
    for (std::int64_t i = 0; i < g.cells(); ++i) {
      for (const Dir8 d : kDir8) {
        const std::int64_t j = g.neighbor(i, d);
        const bool inside = g.contains(g.x_of(i) + wg::core::dx(d), g.y_of(i) + wg::core::dy(d));
        REQUIRE((j != kNoCell) == inside);
        if (inside) {
          REQUIRE(g.neighbor(j, wg::core::opposite(d)) == i);
        }
      }
    }
  }
}

TEST_CASE("core.grid corners and edges") {
  const Grid g = make_grid(5);
  const std::int64_t nw = g.idx(0, 0);
  const std::int64_t se = g.idx(4, 4);
  CHECK(g.neighbor(nw, Dir8::e) == g.idx(1, 0));
  CHECK(g.neighbor(nw, Dir8::s) == g.idx(0, 1));
  CHECK(g.neighbor(nw, Dir8::se) == g.idx(1, 1));
  CHECK(g.neighbor(nw, Dir8::n) == kNoCell);
  CHECK(g.neighbor(nw, Dir8::w) == kNoCell);
  CHECK(g.neighbor(nw, Dir8::sw) == kNoCell);
  CHECK(g.neighbor(se, Dir8::nw) == g.idx(3, 3));
  CHECK(g.neighbor(se, Dir8::e) == kNoCell);
  CHECK(g.neighbor(se, Dir8::s) == kNoCell);
  CHECK(g.neighbor(g.idx(4, 0), Dir8::ne) == kNoCell);
  CHECK(g.neighbor(g.idx(0, 4), Dir8::sw) == kNoCell);

  // A cell on an edge but not a corner has 5 neighbours, an interior cell 8, a corner 3.
  auto count = [&](std::int64_t i) {
    int c = 0;
    for (const Dir8 d : kDir8) {
      c += g.neighbor(i, d) != kNoCell ? 1 : 0;
    }
    return c;
  };
  CHECK(count(nw) == 3);
  CHECK(count(g.idx(2, 0)) == 5);
  CHECK(count(g.idx(2, 2)) == 8);

  int d4 = 0;
  for (const Dir8 d : kDir4) {
    CHECK_FALSE(wg::core::is_diagonal(d));
    d4 += g.neighbor(g.idx(2, 2), d) != kNoCell ? 1 : 0;
  }
  CHECK(d4 == 4);
}

TEST_CASE("core.grid is_border marks the outer ring") {
  const Grid g = make_grid(5);
  int border = 0;
  for (std::int64_t i = 0; i < g.cells(); ++i) {
    border += g.is_border(i) ? 1 : 0;
  }
  CHECK(border == 16);
  CHECK(g.is_border(g.idx(0, 2)));
  CHECK(g.is_border(g.idx(4, 2)));
  CHECK_FALSE(g.is_border(g.idx(1, 1)));
}

TEST_CASE("core.grid validate accepts 2^k + 1 and positive spacing") {
  for (const std::int32_t n : {3, 5, 9, 17, 33, 65, 129, 257, 513, 1025, 2049}) {
    CHECK(wg::core::validate(make_grid(n)).ok());
  }
}

TEST_CASE("core.grid validate rejects bad sizes and spacing") {
  for (const std::int32_t n : {-1, 0, 1, 2, 4, 6, 32, 34, 4097}) {
    const auto s = wg::core::validate(make_grid(n));
    CHECK(s.code == StatusCode::invalid_argument);
    CHECK_FALSE(s.message.empty());
  }
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  for (const double v : {0.0, -1.0, nan, inf}) {
    const Grid bad_dx{.n = 5, .dx_m = v, .hu_m = 1.0};
    const Grid bad_hu{.n = 5, .dx_m = 1.0, .hu_m = v};
    CHECK(wg::core::validate(bad_dx).code == StatusCode::invalid_argument);
    CHECK(wg::core::validate(bad_hu).code == StatusCode::invalid_argument);
  }
}
