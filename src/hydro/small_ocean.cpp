#include "wg/hydro/small_ocean.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/labeling.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

core::Expected<std::int32_t> remove_small_oceans(std::span<float> z, const core::Grid& grid,
                                                 std::int32_t min_area,
                                                 core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "remove_small_oceans: z must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (min_area < 0) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "remove_small_oceans: min_area must be >= 0");
  }
  if (min_area == 0) {
    return 0;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::uint8_t>(cells) + detail::arena_bytes<std::int32_t>(cells) +
          (2 * detail::arena_bytes<std::uint8_t>(cells + 1)),
      resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<std::uint8_t>> ocean = arena->allocate<std::uint8_t>(cells);
  core::Expected<std::span<std::int32_t>> labels = arena->allocate<std::int32_t>(cells);
  core::Expected<std::span<std::uint8_t>> on_edge = arena->allocate<std::uint8_t>(cells + 1);
  core::Expected<std::span<std::uint8_t>> remove = arena->allocate<std::uint8_t>(cells + 1);
  if (!ocean || !labels || !on_edge || !remove) {
    return core::make_error(core::StatusCode::out_of_memory, "remove_small_oceans: scratch");
  }
  for (std::size_t i = 0; i < cells; ++i) {
    (*ocean)[i] = z[i] < terrain::kSeaLevel ? 1 : 0;
  }
  core::Expected<std::int32_t> count =
      num::label_components(*ocean, grid, num::Connectivity::d8, *labels, resource);
  if (!count) {
    return core::make_error(std::move(count.error()));
  }
  core::Expected<std::vector<std::int64_t>> sizes = num::component_sizes(*labels, *count);
  if (!sizes) {
    return core::make_error(std::move(sizes.error()));
  }
  std::fill(on_edge->begin(), on_edge->end(), std::uint8_t{0});
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    if (grid.is_border(i)) {
      (*on_edge)[static_cast<std::size_t>((*labels)[static_cast<std::size_t>(i)])] = 1;
    }
  }
  // Per label: removed or kept. Label 0 is land.
  std::int32_t removed = 0;
  (*remove)[0] = 0;
  for (std::int32_t k = 1; k <= *count; ++k) {
    const auto ku = static_cast<std::size_t>(k);
    const bool small = (*sizes)[ku] < static_cast<std::int64_t>(min_area);
    (*remove)[ku] = small && (*on_edge)[ku] == 0 ? 1 : 0;
    removed += (*remove)[ku];
  }
  for (std::size_t i = 0; i < cells; ++i) {
    if ((*remove)[static_cast<std::size_t>((*labels)[i])] != 0) {
      z[i] = terrain::kSeaLevel;
    }
  }
  return removed;
}

}  // namespace wg::hydro
