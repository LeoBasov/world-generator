#include "wg/core/expected.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <utility>

using wg::core::Expected;
using wg::core::make_error;
using wg::core::StatusCode;

namespace {

Expected<int> half(int v) noexcept {
  if (v % 2 != 0) {
    return make_error(StatusCode::invalid_argument, "odd");
  }
  return v / 2;
}

}  // namespace

TEST_CASE("core.expected holds a value or a status") {
  const Expected<int> ok = half(4);
  REQUIRE(ok.has_value());
  CHECK(*ok == 2);

  const Expected<int> err = half(3);
  REQUIRE_FALSE(err.has_value());
  CHECK(err.error().code == StatusCode::invalid_argument);
  CHECK(err.error().message == "odd");
}

TEST_CASE("core.expected make_error forwards an existing status") {
  const Expected<double> e = make_error(wg::core::make_status(StatusCode::io_error, "disk"));
  REQUIRE_FALSE(e);
  CHECK(e.error().code == StatusCode::io_error);
}

TEST_CASE("core.expected supports move-only values") {
  Expected<std::unique_ptr<int>> e = std::make_unique<int>(7);
  REQUIRE(e);
  const std::unique_ptr<int> p = std::move(*e);
  CHECK(*p == 7);
}
