#include "wg/fractal/peaks.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/labeling.hpp"

namespace wg::fractal {

namespace detail = num::detail;

core::Status find_peak_candidates(std::span<const float> raw_z, const core::Grid& grid,
                                  float threshold, float radius, std::vector<core::Peak>& out,
                                  core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(raw_z, grid,
                           "find_peak_candidates: raw_z must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(threshold) || !std::isfinite(radius)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "find_peak_candidates: threshold and radius must be finite");
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::uint8_t>(cells) + detail::arena_bytes<std::int32_t>(cells),
      resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::uint8_t>> mask = arena->allocate<std::uint8_t>(cells);
  if (!mask) {
    return std::move(mask.error());
  }
  core::Expected<std::span<std::int32_t>> labels = arena->allocate<std::int32_t>(cells);
  if (!labels) {
    return std::move(labels.error());
  }
  for (std::size_t i = 0; i < cells; ++i) {
    (*mask)[i] = raw_z[i] >= threshold ? 1 : 0;
  }
  core::Expected<std::int32_t> count =
      num::label_components(*mask, grid, num::Connectivity::d8, *labels, resource);
  if (!count) {
    return std::move(count.error());
  }

  try {
    // Labels are numbered by their first cell in row-major order, so peak k - 1 belongs to label
    // k. Scanning in index order with a strict > keeps the lowest index among equal maxima.
    out.assign(static_cast<std::size_t>(*count),
               core::Peak{.cell = core::kNoCell, .height_eu = threshold, .radius_cells = radius});
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "find_peak_candidates: peaks");
  }
  for (std::size_t i = 0; i < cells; ++i) {
    const std::int32_t label = (*labels)[i];
    if (label == 0) {
      continue;
    }
    core::Peak& peak = out[static_cast<std::size_t>(label - 1)];
    if (peak.cell == core::kNoCell || raw_z[i] > raw_z[static_cast<std::size_t>(peak.cell)]) {
      peak.cell = static_cast<std::int64_t>(i);
    }
  }
  return {};
}

}  // namespace wg::fractal
