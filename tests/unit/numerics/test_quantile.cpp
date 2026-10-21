#include "wg/numerics/quantile.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/counting_resource.hpp"

using wg::core::StatusCode;

TEST_CASE("numerics.quantile empirical CDF of a permutation is rank / m") {
  const std::vector<float> values = {3.0F, 9.0F, 0.0F, 5.0F, 1.0F, 8.0F, 2.0F, 7.0F, 4.0F, 6.0F};
  std::vector<float> out(values.size());
  REQUIRE(wg::num::empirical_cdf(values, {}, out).ok());
  for (std::size_t i = 0; i < values.size(); ++i) {
    CAPTURE(i);
    CHECK(out[i] == static_cast<float>((values[i] + 1.0) / 10.0));
  }
}

TEST_CASE("numerics.quantile equal values share their CDF value") {
  const std::vector<float> values = {2.0F, 1.0F, 1.0F, 2.0F, 3.0F};
  std::vector<float> out(values.size());
  REQUIRE(wg::num::empirical_cdf(values, {}, out).ok());
  CHECK(out == std::vector<float>{0.8F, 0.4F, 0.4F, 0.8F, 1.0F});
}

TEST_CASE("numerics.quantile mask selects the values and leaves the rest") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<float> values = {4.0F, nan, 1.0F, 3.0F};
  const std::vector<std::uint8_t> mask = {1, 0, 1, 1};
  std::vector<float> out(values.size(), -1.0F);
  REQUIRE(wg::num::empirical_cdf(values, mask, out).ok());
  CHECK(out[0] == 1.0F);
  CHECK(out[1] == -1.0F);
  CHECK(out[2] == static_cast<float>(1.0 / 3.0));
  CHECK(out[3] == static_cast<float>(2.0 / 3.0));

  CHECK(wg::num::empirical_cdf(values, {}, out).code == StatusCode::invalid_argument);
  const std::vector<std::uint8_t> short_mask = {1};
  CHECK(wg::num::empirical_cdf(values, short_mask, out).code == StatusCode::invalid_argument);
  std::vector<float> short_out(2);
  CHECK(wg::num::empirical_cdf(values, mask, short_out).code == StatusCode::invalid_argument);
  std::vector<float> none;
  CHECK(wg::num::empirical_cdf({}, {}, none).ok());

  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::empirical_cdf(values, mask, out, resource).code == StatusCode::out_of_memory);
}

TEST_CASE("numerics.quantile interpolates between order statistics") {
  const std::vector<double> sorted = {0.0, 10.0, 20.0};
  CHECK(*wg::num::quantile(sorted, 0.0) == 0.0);
  CHECK(*wg::num::quantile(sorted, 0.25) == 5.0);
  CHECK(*wg::num::quantile(sorted, 0.5) == 10.0);
  CHECK(*wg::num::quantile(sorted, 1.0) == 20.0);
  const std::vector<double> single = {7.0};
  CHECK(*wg::num::quantile(single, 0.3) == 7.0);
  CHECK(wg::num::quantile({}, 0.5).error().code == StatusCode::invalid_argument);
  CHECK(wg::num::quantile(sorted, 1.5).error().code == StatusCode::invalid_argument);
  CHECK(wg::num::quantile(sorted, std::nan("")).error().code == StatusCode::invalid_argument);
}

TEST_CASE("numerics.quantile mapping follows the target distribution") {
  const std::vector<float> values = {4.0F, 0.0F, 2.0F, 1.0F, 3.0F};
  const std::vector<double> target = {0.0, 100.0};
  std::vector<float> out(values.size());
  REQUIRE(wg::num::quantile_map(values, {}, target, out).ok());
  // F = (v + 1) / 5, mapped linearly onto [0, 100].
  CHECK(out == std::vector<float>{100.0F, 20.0F, 60.0F, 40.0F, 80.0F});

  const std::vector<double> unsorted = {1.0, 0.0};
  CHECK(wg::num::quantile_map(values, {}, unsorted, out).code == StatusCode::invalid_argument);
  CHECK(wg::num::quantile_map(values, {}, {}, out).code == StatusCode::invalid_argument);
}
