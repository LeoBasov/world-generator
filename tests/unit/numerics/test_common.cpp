#include "numerics/common.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "support/fixtures.hpp"

TEST_CASE("num.detail.neighbors8 equals grid.neighbor in kDir8 order") {
  for (const std::int32_t n : {3, 5, 9}) {
    CAPTURE(n);
    const wg::core::Grid grid = wg::test::make_grid(n);
    for (std::int64_t i = 0; i < grid.cells(); ++i) {
      const auto all = wg::num::detail::neighbors8(grid, i);
      for (std::size_t k = 0; k < wg::core::kDir8.size(); ++k) {
        CHECK(all[k] == grid.neighbor(i, wg::core::kDir8[k]));
      }
    }
  }
}
