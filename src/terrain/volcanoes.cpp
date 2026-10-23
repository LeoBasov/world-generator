#include "wg/terrain/volcanoes.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/labeling.hpp"

namespace wg::terrain {

namespace detail = num::detail;

namespace {

struct Area {
  double sum_x = 0.0;
  double sum_y = 0.0;
  std::int64_t cells = 0;
  double best_d2 = 0.0;
};

}  // namespace

core::Status place_volcanoes(std::span<const float> volcanism, const core::Grid& grid,
                             float threshold, std::vector<core::Volcano>& out,
                             core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(volcanism, grid,
                           "place_volcanoes: volcanism must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(threshold)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "place_volcanoes: threshold must be finite");
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
    (*mask)[i] = volcanism[i] >= threshold ? 1 : 0;
  }
  core::Expected<std::int32_t> count =
      num::label_components(*mask, grid, num::Connectivity::d8, *labels, resource);
  if (!count) {
    return std::move(count.error());
  }

  std::vector<Area> areas;
  try {
    areas.resize(static_cast<std::size_t>(*count));
    out.assign(static_cast<std::size_t>(*count),
               core::Volcano{.cell = core::kNoCell, .magma_connected = true});
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "place_volcanoes: volcanoes");
  }
  // Labels are numbered by their first cell in row-major order, so volcano k - 1 belongs to
  // label k (as for the peaks of C2).
  for (std::size_t i = 0; i < cells; ++i) {
    const std::int32_t label = (*labels)[i];
    if (label != 0) {
      Area& a = areas[static_cast<std::size_t>(label - 1)];
      a.sum_x += grid.x_of(static_cast<std::int64_t>(i));
      a.sum_y += grid.y_of(static_cast<std::int64_t>(i));
      ++a.cells;
    }
  }
  // Nearest cell to the centroid; scanning in index order with a strict < keeps the lower index.
  for (std::size_t i = 0; i < cells; ++i) {
    const std::int32_t label = (*labels)[i];
    if (label == 0) {
      continue;
    }
    const auto k = static_cast<std::size_t>(label - 1);
    Area& a = areas[k];
    const auto count_d = static_cast<double>(a.cells);
    const double ddx = grid.x_of(static_cast<std::int64_t>(i)) - (a.sum_x / count_d);
    const double ddy = grid.y_of(static_cast<std::int64_t>(i)) - (a.sum_y / count_d);
    const double d2 = (ddx * ddx) + (ddy * ddy);
    if (out[k].cell == core::kNoCell || d2 < a.best_d2) {
      out[k].cell = static_cast<std::int64_t>(i);
      a.best_d2 = d2;
    }
  }
  return {};
}

}  // namespace wg::terrain
