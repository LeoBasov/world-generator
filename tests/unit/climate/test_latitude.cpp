#include "wg/climate/latitude.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::climate::latitude_of_row;
using wg::core::StatusCode;
using wg::params::Poles;

namespace {

constexpr double kHalfPi = std::numbers::pi / 2.0;
constexpr std::array<Poles, 4> kAllPoles = {Poles::none, Poles::north, Poles::south, Poles::both};

std::vector<float> latitude(Poles poles, const wg::core::Grid& grid, wg::core::Executor& exec) {
  std::vector<float> phi(static_cast<std::size_t>(grid.cells()), -1.0F);
  REQUIRE(wg::climate::fill_latitude(poles, grid, phi, exec).ok());
  return phi;
}

}  // namespace

static_assert(latitude_of_row(Poles::north, 17, 0) == kHalfPi);
static_assert(latitude_of_row(Poles::north, 17, 16) == 0.0);
static_assert(latitude_of_row(Poles::south, 17, 0) == 0.0);
static_assert(latitude_of_row(Poles::both, 17, 8) == 0.0);
static_assert(latitude_of_row(Poles::none, 17, 3) == 0.0);

TEST_CASE("climate.latitude_of_row matches each pole option") {
  for (const std::int32_t n : {17, 33, 65}) {
    CAPTURE(n);
    const std::int32_t last = n - 1;
    const std::int32_t mid = last / 2;
    CHECK(latitude_of_row(Poles::north, n, 0) == kHalfPi);
    CHECK(latitude_of_row(Poles::north, n, last) == 0.0);
    CHECK(latitude_of_row(Poles::south, n, 0) == 0.0);
    CHECK(latitude_of_row(Poles::south, n, last) == kHalfPi);
    CHECK(latitude_of_row(Poles::both, n, 0) == kHalfPi);
    CHECK(latitude_of_row(Poles::both, n, last) == kHalfPi);
    CHECK(latitude_of_row(Poles::both, n, mid) == 0.0);
    for (std::int32_t y = 0; y < n; ++y) {
      CAPTURE(y);
      CHECK(latitude_of_row(Poles::none, n, y) == 0.0);
      // phi is linear in the distance from the equator.
      CHECK(latitude_of_row(Poles::north, n, y) ==
            doctest::Approx(kHalfPi * (last - y) / last).epsilon(1e-15));
      CHECK(latitude_of_row(Poles::south, n, y) == latitude_of_row(Poles::north, n, last - y));
      CHECK(latitude_of_row(Poles::both, n, y) == latitude_of_row(Poles::both, n, last - y));
      for (const Poles p : kAllPoles) {
        CHECK(latitude_of_row(p, n, y) >= 0.0);
        CHECK(latitude_of_row(p, n, y) <= kHalfPi);
      }
      if (y > 0) {
        CHECK(latitude_of_row(Poles::north, n, y) < latitude_of_row(Poles::north, n, y - 1));
        if (y <= mid) {
          CHECK(latitude_of_row(Poles::both, n, y) < latitude_of_row(Poles::both, n, y - 1));
        } else {
          CHECK(latitude_of_row(Poles::both, n, y) > latitude_of_row(Poles::both, n, y - 1));
        }
      }
    }
  }
}

TEST_CASE("climate.fill_latitude writes the row latitude into every cell") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  for (const std::int32_t n : {17, 33, 65}) {
    const wg::core::Grid grid = wg::test::make_grid(n);
    for (const Poles p : kAllPoles) {
      CAPTURE(n);
      CAPTURE(static_cast<int>(p));
      const std::vector<float> phi = latitude(p, grid, serial);
      for (std::int32_t y = 0; y < n; ++y) {
        const auto expected = static_cast<float>(latitude_of_row(p, n, y));
        for (std::int32_t x = 0; x < n; ++x) {
          REQUIRE(phi[static_cast<std::size_t>(grid.idx(x, y))] == expected);
        }
      }
      CHECK(latitude(p, grid, **pool) == phi);
    }
  }
}

TEST_CASE("climate.fill_latitude rejects bad arguments and honours cancel") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  std::vector<float> phi(static_cast<std::size_t>(grid.cells()) - 1);
  CHECK(wg::climate::fill_latitude(Poles::north, grid, phi, serial).code ==
        StatusCode::invalid_argument);
  std::vector<float> bad(64);
  CHECK(wg::climate::fill_latitude(Poles::north, wg::test::make_grid(8), bad, serial).code ==
        StatusCode::invalid_argument);
  phi.resize(static_cast<std::size_t>(grid.cells()));
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::climate::fill_latitude(Poles::north, grid, phi, serial, &cancel).code ==
        StatusCode::cancelled);
}

TEST_CASE("climate.latitude_term is -Delta T sin^2(phi)") {
  CHECK(wg::climate::latitude_term(0.0, 75.0) == 0.0);
  CHECK(wg::climate::latitude_term(kHalfPi, 75.0) == doctest::Approx(-75.0).epsilon(1e-12));
  CHECK(wg::climate::latitude_term(1.0, 0.0) == 0.0);
  for (int k = 0; k <= 20; ++k) {
    const double phi = kHalfPi * k / 20.0;
    const double s = std::sin(phi);
    CHECK(std::abs(wg::climate::latitude_term(phi, 40.0) - (-40.0 * s * s)) < 1e-4);
    if (k > 0) {
      CHECK(wg::climate::latitude_term(phi, 40.0) <
            wg::climate::latitude_term(kHalfPi * (k - 1) / 20.0, 40.0));
    }
  }
}
