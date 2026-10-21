#include "wg/numerics/histogram.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::SerialExecutor;
using wg::core::StatusCode;

TEST_CASE("numerics.histogram bins, edges and out-of-range values") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<float> values = {0.0F, 0.25F, 0.3F, 0.999F, 1.0F, -0.1F, 1.1F, nan, 0.5F};
  SerialExecutor exec;
  const auto h = wg::num::histogram(values, {}, {.lo = 0.0, .hi = 1.0, .bins = 4}, exec);
  REQUIRE(h);
  CHECK(h->lo == 0.0);
  CHECK(h->hi == 1.0);
  CHECK(h->counts == std::vector<std::int64_t>{1, 2, 1, 2});
  CHECK(h->underflow == 1);
  CHECK(h->overflow == 1);
  CHECK(h->nan == 1);
  CHECK(h->total() == static_cast<std::int64_t>(values.size()));

  const std::vector<std::uint8_t> mask = {1, 1, 0, 0, 0, 0, 0, 0, 1};
  const auto masked = wg::num::histogram(values, mask, {.lo = 0.0, .hi = 1.0, .bins = 4}, exec);
  REQUIRE(masked);
  CHECK(masked->counts == std::vector<std::int64_t>{1, 1, 1, 0});
  CHECK(masked->total() == 3);
}

TEST_CASE("numerics.histogram is identical on a thread pool") {
  const wg::core::Grid grid = wg::test::make_grid(513);
  const std::vector<float> values = wg::test::white_noise(grid, 5);
  SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(8);
  REQUIRE(pool);
  const wg::num::HistogramParams params{.lo = -0.4, .hi = 0.4, .bins = 64};
  const auto a = wg::num::histogram(values, {}, params, serial);
  const auto b = wg::num::histogram(values, {}, params, **pool);
  REQUIRE(a);
  REQUIRE(b);
  CHECK(a->counts == b->counts);
  CHECK(a->underflow == b->underflow);
  CHECK(a->overflow == b->overflow);
  CHECK(a->total() == grid.cells());
  CHECK(a->underflow > 0);
  CHECK(a->overflow > 0);
}

TEST_CASE("numerics.histogram rejects bad parameters") {
  const std::vector<float> values = {1.0F, 2.0F};
  const std::vector<std::uint8_t> short_mask = {1};
  SerialExecutor exec;
  const double inf = std::numeric_limits<double>::infinity();
  CHECK(wg::num::histogram(values, {}, {.lo = 1.0, .hi = 1.0, .bins = 4}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::histogram(values, {}, {.lo = 0.0, .hi = inf, .bins = 4}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::histogram(values, {}, {.lo = 0.0, .hi = 1.0, .bins = 0}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::histogram(values, short_mask, {}, exec).error().code ==
        StatusCode::invalid_argument);
  const auto empty = wg::num::histogram({}, {}, {}, exec);
  REQUIRE(empty);
  CHECK(empty->total() == 0);
  CHECK(empty->counts.size() == 10);
}

TEST_CASE("numerics.histogram value range skips NaN, infinities and masked values") {
  const float inf = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<float> values = {3.0F, -inf, -2.0F, nan, 7.5F, inf, 100.0F};
  const std::vector<std::uint8_t> mask = {1, 1, 1, 1, 1, 1, 0};
  SerialExecutor exec;
  const auto r = wg::num::value_range(values, mask, exec);
  REQUIRE(r);
  CHECK(r->min == -2.0);
  CHECK(r->max == 7.5);
  CHECK(r->count == 3);

  const std::vector<float> only_nan = {nan, nan};
  const auto none = wg::num::value_range(only_nan, {}, exec);
  REQUIRE(none);
  CHECK(none->count == 0);
  const std::vector<std::uint8_t> short_mask = {1};
  CHECK(wg::num::value_range(values, short_mask, exec).error().code ==
        StatusCode::invalid_argument);

  // Chunks without values do not disturb the combination.
  const wg::core::Grid grid = wg::test::make_grid(513);
  std::vector<float> big(static_cast<std::size_t>(grid.cells()), nan);
  big[200000] = 4.0F;
  big.back() = -1.0F;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const auto br = wg::num::value_range(big, {}, **pool);
  REQUIRE(br);
  CHECK(br->min == -1.0);
  CHECK(br->max == 4.0);
  CHECK(br->count == 2);
}
