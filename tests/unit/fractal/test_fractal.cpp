#include "wg/fractal/fractal.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/io/hash.hpp"

using wg::core::Grid;
using wg::core::RngKey;
using wg::core::SerialExecutor;
using wg::core::StatusCode;
using wg::fractal::ControlGrid;
using wg::fractal::kFreeControl;
using wg::params::FieldGenParams;

namespace {

const RngKey kKey = wg::core::stage_key(11, "test.fractal", 0);

FieldGenParams noise_free() {
  FieldGenParams p;
  p.x_variance = 0.0;
  p.y_variance = 0.0;
  return p;
}

std::vector<float> cells_of(const Grid& grid) {
  return std::vector<float>(static_cast<std::size_t>(grid.cells()), 0.0F);
}

std::vector<float> controls_of(ControlGrid c) {
  return std::vector<float>(static_cast<std::size_t>(wg::fractal::control_count(c)), 0.0F);
}

std::vector<float> generate(const Grid& grid, const FieldGenParams& p, RngKey key,
                            wg::core::Executor& exec) {
  std::vector<float> z = cells_of(grid);
  REQUIRE(wg::fractal::generate_field(grid, p, key, {}, z, exec).ok());
  return z;
}

std::uint64_t hash_of(std::span<const float> z) { return wg::io::xxh3_64(std::as_bytes(z)); }

}  // namespace

TEST_CASE("fractal.control_grid follows the mesh and is limited by the grid") {
  for (std::int32_t n = 17; n <= 257; n = (2 * n) - 1) {
    for (std::int32_t m = 1; m <= wg::fractal::kMaxMesh; ++m) {
      CAPTURE(n);
      CAPTURE(m);
      const auto c = wg::fractal::control_grid(wg::test::make_grid(n), m);
      REQUIRE(c);
      const std::int32_t cells = std::min(std::int32_t{1} << (m - 1), n - 1);
      CHECK(c->cells == cells);
      CHECK(c->spacing * c->cells == n - 1);
      CHECK(wg::fractal::control_count(*c) == static_cast<std::int64_t>(cells + 1) * (cells + 1));
    }
  }
  CHECK(*wg::fractal::control_grid(wg::test::make_grid(17), 6) == ControlGrid{16, 1});
  CHECK(*wg::fractal::control_grid(wg::test::make_grid(257), 6) == ControlGrid{32, 8});
  CHECK(wg::fractal::control_grid(wg::test::make_grid(65), 0).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::control_grid(wg::test::make_grid(65), 7).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::control_grid(wg::test::make_grid(64), 3).error().code ==
        StatusCode::invalid_argument);
}

TEST_CASE("fractal.pin_edges marks the control points of the chosen edges") {
  const ControlGrid c{4, 16};
  std::vector<std::int8_t> pins(25, kFreeControl);
  REQUIRE(
      wg::fractal::pin_edges(c, wg::fractal::kEdgeNorth | wg::fractal::kEdgeWest, 0, pins).ok());
  for (std::int32_t cy = 0; cy <= 4; ++cy) {
    for (std::int32_t cx = 0; cx <= 4; ++cx) {
      CAPTURE(cx);
      CAPTURE(cy);
      const std::int8_t expected = (cx == 0 || cy == 0) ? 0 : kFreeControl;
      CHECK(pins[static_cast<std::size_t>((cy * 5) + cx)] == expected);
    }
  }
  REQUIRE(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, 2, pins).ok());
  CHECK(std::count(pins.begin(), pins.end(), std::int8_t{2}) == 16);
  CHECK(std::count(pins.begin(), pins.end(), kFreeControl) == 9);

  CHECK(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, 5, pins).code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, -2, pins).code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, 0, std::span(pins).first(24)).code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::pin_edges(ControlGrid{3, 16}, wg::fractal::kAllEdges, 0, pins).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("fractal.sample_controls draws inside the bins and honours pins") {
  const ControlGrid c{8, 16};
  FieldGenParams p;
  p.min = 1.0;
  p.max = 401.0;
  p.mesh = 4;
  std::vector<float> free = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, free).ok());
  for (const float v : free) {
    CHECK(v >= 1.0F);
    CHECK(v <= 401.0F);
  }

  // Pinning the edges to the lowest bin moves only the edge points, into [min, min + delta].
  std::vector<std::int8_t> pins(free.size(), kFreeControl);
  REQUIRE(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, 0, pins).ok());
  std::vector<float> pinned = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, pins, pinned).ok());
  for (std::size_t i = 0; i < pinned.size(); ++i) {
    CAPTURE(i);
    if (pins[i] == 0) {
      CHECK(pinned[i] >= 1.0F);
      CHECK(pinned[i] <= 81.0F);
    } else {
      CHECK(pinned[i] == free[i]);
    }
  }

  // A weight of zero is never chosen.
  p.weights = {0.0, 0.0, 1.0, 0.0, 0.0};
  std::vector<float> middle = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, middle).ok());
  for (const float v : middle) {
    CHECK(v >= 161.0F);
    CHECK(v <= 241.0F);
  }
}

TEST_CASE("fractal.sample_controls ignores the weights with mesh 1") {
  const ControlGrid c{1, 64};
  FieldGenParams p;
  p.mesh = 1;
  std::vector<float> a = controls_of(c);
  std::vector<float> b = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, a).ok());
  p.weights = {1.0, 0.0, 0.0, 0.0, 0.0};
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, b).ok());
  CHECK(a == b);
}

TEST_CASE("fractal.sample_controls rejects bad input") {
  const ControlGrid c{2, 8};
  std::vector<float> values = controls_of(c);
  FieldGenParams p;
  p.min = 5.0;
  p.max = 4.0;
  CHECK(wg::fractal::sample_controls(p, c, kKey, {}, values).code == StatusCode::invalid_argument);
  p = FieldGenParams{};
  p.weights = {0.0, 0.0, 0.0, 0.0, 0.0};
  CHECK(wg::fractal::sample_controls(p, c, kKey, {}, values).code == StatusCode::invalid_argument);
  p = FieldGenParams{};
  CHECK(wg::fractal::sample_controls(p, c, kKey, {}, std::span(values).first(8)).code ==
        StatusCode::invalid_argument);
  std::vector<std::int8_t> pins(9, kFreeControl);
  pins[4] = 5;
  CHECK(wg::fractal::sample_controls(p, c, kKey, pins, values).code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::sample_controls(p, c, kKey, std::span(pins).first(3), values).code ==
        StatusCode::invalid_argument);
  CHECK(wg::fractal::sample_controls(p, ControlGrid{0, 8}, kKey, {}, values).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("fractal.diamond_square without variance interpolates the controls noise-free") {
  const Grid grid = wg::test::make_grid(65);
  const FieldGenParams p = noise_free();
  const ControlGrid c = *wg::fractal::control_grid(grid, 3);
  std::vector<float> controls = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, controls).ok());
  SerialExecutor exec;
  std::vector<float> a = cells_of(grid);
  std::vector<float> b = cells_of(grid);
  REQUIRE(wg::fractal::diamond_square(controls, c, grid, p, kKey, a, exec).ok());
  REQUIRE(wg::fractal::diamond_square(controls, c, grid, p, kKey.derive(99), b, exec).ok());
  // No random draw is used: another key gives the same field.
  CHECK(a == b);
  // The control points keep their values.
  for (std::int32_t cy = 0; cy <= c.cells; ++cy) {
    for (std::int32_t cx = 0; cx <= c.cells; ++cx) {
      CHECK(a[static_cast<std::size_t>(grid.idx(cx * c.spacing, cy * c.spacing))] ==
            controls[static_cast<std::size_t>((cy * (c.cells + 1)) + cx)]);
    }
  }
  // Every value is a mean of control values.
  const auto [lo, hi] = std::minmax_element(controls.begin(), controls.end());
  for (const float v : a) {
    CHECK(v >= *lo);
    CHECK(v <= *hi);
  }
  // Constant controls give a constant field.
  std::fill(controls.begin(), controls.end(), 42.0F);
  REQUIRE(wg::fractal::diamond_square(controls, c, grid, p, kKey, a, exec).ok());
  CHECK(std::all_of(a.begin(), a.end(), [](float v) { return v == 42.0F; }));
}

TEST_CASE("fractal.generate_field is identical for any executor and depends on the key") {
  const Grid grid = wg::test::make_grid(129);
  const FieldGenParams p;
  SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(8);
  REQUIRE(pool);
  const std::vector<float> a = generate(grid, p, kKey, serial);
  const std::vector<float> b = generate(grid, p, kKey, **pool);
  const std::vector<float> c = generate(grid, p, kKey, serial);
  const std::vector<float> d = generate(grid, p, kKey.derive(1), serial);
  CHECK(a == b);
  CHECK(a == c);
  CHECK(a != d);
}

TEST_CASE("fractal.generate_field known-answer hash") {
  // Bit-identical on every platform (R1): det_exp and the counter RNG only, no libm call.
  const Grid grid = wg::test::make_grid(33);
  FieldGenParams p;
  p.min = 1.0;
  p.max = 400.0;
  p.x_variance = 2400.0;
  p.y_variance = 600.0;
  p.hurst = 0.7;
  p.mesh = 2;
  p.weights = {6.0, 1.0, 1.0, 1.0, 1.0};
  SerialExecutor exec;
  const std::vector<float> z =
      generate(grid, p, wg::core::stage_key(42, "c02_seed_primary", 0), exec);
  CHECK(hash_of(z) == 0x47BCC4FBB374A0FCULL);
}

TEST_CASE("fractal.generate_field clamps to the field range") {
  const Grid grid = wg::test::make_grid(65);
  FieldGenParams p;
  p.min = 0.0;
  p.max = 100.0;
  p.x_variance = 3200.0;
  p.y_variance = 3200.0;
  p.noise_scale = 2.0;
  SerialExecutor exec;
  const ControlGrid c = *wg::fractal::control_grid(grid, p.mesh);
  std::vector<float> controls = controls_of(c);
  REQUIRE(wg::fractal::sample_controls(p, c, kKey, {}, controls).ok());
  std::vector<float> raw = cells_of(grid);
  REQUIRE(wg::fractal::diamond_square(controls, c, grid, p, kKey, raw, exec).ok());
  const auto [lo, hi] = std::minmax_element(raw.begin(), raw.end());
  CHECK(*lo < 0.0F);
  CHECK(*hi > 100.0F);

  const std::vector<float> z = generate(grid, p, kKey, exec);
  for (std::size_t i = 0; i < z.size(); ++i) {
    CHECK(z[i] == std::clamp(raw[i], 0.0F, 100.0F));
  }
}

TEST_CASE("fractal.generate_field with pinned edges keeps the edge controls low") {
  const Grid grid = wg::test::make_grid(65);
  FieldGenParams p = noise_free();
  p.min = 1.0;
  p.max = 401.0;
  const ControlGrid c = *wg::fractal::control_grid(grid, p.mesh);
  std::vector<std::int8_t> pins(static_cast<std::size_t>(wg::fractal::control_count(c)),
                                kFreeControl);
  REQUIRE(wg::fractal::pin_edges(c, wg::fractal::kAllEdges, 0, pins).ok());
  SerialExecutor exec;
  std::vector<float> z = cells_of(grid);
  std::vector<float> free = cells_of(grid);
  REQUIRE(wg::fractal::generate_field(grid, p, kKey, pins, z, exec).ok());
  REQUIRE(wg::fractal::generate_field(grid, p, kKey, {}, free, exec).ok());
  // Edge control points lie in the lowest bin; interior control points are unchanged.
  for (std::int32_t cy = 0; cy <= c.cells; ++cy) {
    for (std::int32_t cx = 0; cx <= c.cells; ++cx) {
      const auto i = static_cast<std::size_t>(grid.idx(cx * c.spacing, cy * c.spacing));
      if (cx == 0 || cy == 0 || cx == c.cells || cy == c.cells) {
        CHECK(z[i] <= 81.0F);
      } else {
        CHECK(z[i] == free[i]);
      }
    }
  }
}

TEST_CASE("fractal.diamond_square rejects bad input") {
  const Grid grid = wg::test::make_grid(33);
  const ControlGrid c = *wg::fractal::control_grid(grid, 3);
  std::vector<float> controls = controls_of(c);
  std::vector<float> z = cells_of(grid);
  SerialExecutor exec;
  FieldGenParams p;
  const auto run = [&](const FieldGenParams& params, ControlGrid cg, std::span<const float> cv,
                       std::span<float> out) {
    return wg::fractal::diamond_square(cv, cg, grid, params, kKey, out, exec).code;
  };
  CHECK(run(p, ControlGrid{4, 4}, controls, z) == StatusCode::invalid_argument);
  CHECK(run(p, c, std::span(controls).first(3), z) == StatusCode::invalid_argument);
  CHECK(run(p, c, controls, std::span(z).first(10)) == StatusCode::invalid_argument);
  p.hurst = 1.0;
  CHECK(run(p, c, controls, z) == StatusCode::invalid_argument);
  p = FieldGenParams{};
  p.x_variance = -1.0;
  CHECK(run(p, c, controls, z) == StatusCode::invalid_argument);
  p = FieldGenParams{};
  p.noise_scale = -1.0;
  CHECK(run(p, c, controls, z) == StatusCode::invalid_argument);
  CHECK(wg::fractal::clamp_field(z, 2.0, 1.0, exec).code == StatusCode::invalid_argument);
  p = FieldGenParams{};
  p.mesh = 9;
  CHECK(wg::fractal::generate_field(grid, p, kKey, {}, z, exec).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("fractal.generate_field observes cancel and reports allocation failure") {
  const Grid grid = wg::test::make_grid(65);
  const FieldGenParams p;
  std::vector<float> z = cells_of(grid);
  SerialExecutor exec;
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::fractal::generate_field(grid, p, kKey, {}, z, exec, &cancel).code ==
        StatusCode::cancelled);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::fractal::generate_field(grid, p, kKey, {}, z, exec, nullptr, resource).code ==
        StatusCode::out_of_memory);
}
