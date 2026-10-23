#include "wg/fractal/peaks.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"

using wg::core::Grid;
using wg::core::Peak;
using wg::core::StatusCode;
using wg::fractal::find_peak_candidates;
using wg::fractal::kPeakElevation;

namespace {

constexpr float kRadius = 3.0F;

std::vector<float> flat(const Grid& grid, float z = 200.0F) {
  return std::vector<float>(static_cast<std::size_t>(grid.cells()), z);
}

void set(std::vector<float>& z, const Grid& grid, std::int32_t x, std::int32_t y, float v) {
  z[static_cast<std::size_t>(grid.idx(x, y))] = v;
}

std::vector<Peak> peaks_of(const std::vector<float>& z, const Grid& grid) {
  std::vector<Peak> peaks{Peak{.cell = 99}};  // replaced, not appended to
  REQUIRE(find_peak_candidates(z, grid, kPeakElevation, kRadius, peaks).ok());
  return peaks;
}

}  // namespace

TEST_CASE("fractal.find_peak_candidates finds nothing below the threshold") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  set(z, grid, 4, 4, 399.99F);
  CHECK(peaks_of(z, grid).empty());
}

TEST_CASE("fractal.find_peak_candidates puts one peak at the summit of a plateau") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  for (std::int32_t y = 3; y <= 6; ++y) {
    for (std::int32_t x = 5; x <= 9; ++x) {
      set(z, grid, x, y, 410.0F);
    }
  }
  set(z, grid, 7, 5, 455.0F);
  const std::vector<Peak> peaks = peaks_of(z, grid);
  REQUIRE(peaks.size() == 1);
  CHECK(peaks[0] ==
        Peak{.cell = grid.idx(7, 5), .height_eu = kPeakElevation, .radius_cells = kRadius});
}

TEST_CASE("fractal.find_peak_candidates counts the threshold itself") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  set(z, grid, 2, 2, kPeakElevation);
  const std::vector<Peak> peaks = peaks_of(z, grid);
  REQUIRE(peaks.size() == 1);
  CHECK(peaks[0].cell == grid.idx(2, 2));
}

TEST_CASE("fractal.find_peak_candidates orders separate areas by their first cell") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  set(z, grid, 12, 10, 420.0F);  // second area in row-major order
  set(z, grid, 13, 10, 430.0F);
  set(z, grid, 3, 2, 401.0F);  // first area
  set(z, grid, 3, 3, 450.0F);
  set(z, grid, 0, 16, 405.0F);  // third area, on the map edge
  const std::vector<Peak> peaks = peaks_of(z, grid);
  REQUIRE(peaks.size() == 3);
  CHECK(peaks[0].cell == grid.idx(3, 3));
  CHECK(peaks[1].cell == grid.idx(13, 10));
  CHECK(peaks[2].cell == grid.idx(0, 16));
}

TEST_CASE("fractal.find_peak_candidates joins diagonal neighbours (D8)") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  set(z, grid, 4, 4, 401.0F);
  set(z, grid, 5, 5, 402.0F);
  set(z, grid, 6, 6, 403.0F);
  const std::vector<Peak> peaks = peaks_of(z, grid);
  REQUIRE(peaks.size() == 1);
  CHECK(peaks[0].cell == grid.idx(6, 6));
}

TEST_CASE("fractal.find_peak_candidates breaks ties by the lower cell index") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = flat(grid);
  set(z, grid, 6, 8, 420.0F);
  set(z, grid, 7, 8, 420.0F);
  set(z, grid, 5, 9, 420.0F);
  const std::vector<Peak> peaks = peaks_of(z, grid);
  REQUIRE(peaks.size() == 1);
  CHECK(peaks[0].cell == grid.idx(6, 8));
}

TEST_CASE("fractal.find_peak_candidates rejects bad arguments") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<Peak> peaks;
  const std::vector<float> short_z(static_cast<std::size_t>(grid.cells()) - 1, 0.0F);
  CHECK(find_peak_candidates(short_z, grid, kPeakElevation, kRadius, peaks).code ==
        StatusCode::invalid_argument);
  const std::vector<float> z = flat(grid);
  CHECK(find_peak_candidates(z, wg::test::make_grid(16), kPeakElevation, kRadius, peaks).code ==
        StatusCode::invalid_argument);
  CHECK(find_peak_candidates(z, grid, kPeakElevation, std::numeric_limits<float>::infinity(), peaks)
            .code == StatusCode::invalid_argument);
}

TEST_CASE("fractal.find_peak_candidates reports out_of_memory") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = flat(grid, 450.0F);
  wg::test::CountingResource resource;
  resource.fail = true;
  std::vector<Peak> peaks;
  CHECK(find_peak_candidates(z, grid, kPeakElevation, kRadius, peaks, resource).code ==
        StatusCode::out_of_memory);
}
