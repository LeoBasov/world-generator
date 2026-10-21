#include "wg/core/field_view.hpp"

#include <doctest/doctest.h>

#include <array>
#include <span>
#include <type_traits>

using wg::core::FieldView;
using wg::core::Grid;

TEST_CASE("core.field_view (x, y) and [i] address the same element") {
  std::array<float, 9> buffer{};
  const Grid g{.n = 3, .dx_m = 1.0, .hu_m = 1.0};
  const FieldView<float> v{std::span<float>(buffer), g};
  CHECK(v.size() == 9);

  v(2, 1) = 5.0F;
  CHECK(buffer[5] == 5.0F);
  CHECK(v[5] == 5.0F);
  v[7] = 3.0F;
  CHECK(v(1, 2) == 3.0F);
}

TEST_CASE("core.field_view as_const views the same data read-only") {
  std::array<int, 9> buffer{};
  const FieldView<int> v{std::span<int>(buffer), Grid{.n = 3, .dx_m = 1.0, .hu_m = 1.0}};
  const FieldView<const int> c = v.as_const();
  static_assert(std::is_same_v<decltype(c(0, 0)), const int&>);
  CHECK(c.data.data() == buffer.data());
  CHECK(c.grid == v.grid);
  v(1, 1) = 4;
  CHECK(c(1, 1) == 4);
}
