#include "wg/hydro/priority_flood.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/heap.hpp"
#include "wg/numerics/neighborhood.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

// Smallest float32 >= base + epsilon, the sum taken in float64.
float raise_above(float base, float epsilon) noexcept {
  const double target = static_cast<double>(base) + static_cast<double>(epsilon);
  auto raised = static_cast<float>(target);
  if (static_cast<double>(raised) < target) {
    raised = std::nextafter(raised, std::numeric_limits<float>::infinity());
  }
  return raised;
}

// Shared flood: epsilon 0 gives the plain fill w_n = max(z_n, w_c).
core::Status flood(std::span<const float> z, const core::Grid& grid, std::span<float> out,
                   float epsilon, std::string_view name, core::MemoryResource& resource) noexcept {
  if (core::Status s =
          detail::first_error({core::validate(grid), detail::check_field(z, grid, name),
                               detail::check_field(out, grid, name)});
      !s.ok()) {
    return s;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  for (std::size_t i = 0; i < cells; ++i) {
    if (!std::isfinite(z[i])) {
      return core::make_status(core::StatusCode::invalid_argument, name);
    }
  }
  core::Expected<num::IndexHeap<float>> heap = num::IndexHeap<float>::create(cells, resource);
  if (!heap) {
    return std::move(heap.error());
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<std::uint8_t>(cells), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::uint8_t>> closed = arena->allocate<std::uint8_t>(cells);
  if (!closed) {
    return std::move(closed.error());
  }
  std::fill(closed->begin(), closed->end(), std::uint8_t{0});

  // Seeds: every ocean cell, or the lowest cell of a map without ocean.
  std::size_t lowest = 0;
  bool any_ocean = false;
  for (std::size_t i = 0; i < cells; ++i) {
    out[i] = z[i];
    if (z[i] < terrain::kSeaLevel) {
      any_ocean = true;
      (*closed)[i] = 1;
      if (core::Status s = heap->push(z[i], static_cast<std::int64_t>(i)); !s.ok()) {
        return s;
      }
    }
    if (z[i] < z[lowest]) {
      lowest = i;
    }
  }
  if (!any_ocean) {
    (*closed)[lowest] = 1;
    if (core::Status s = heap->push(z[lowest], static_cast<std::int64_t>(lowest)); !s.ok()) {
      return s;
    }
  }

  while (!heap->empty()) {
    const num::HeapEntry<float> c = heap->pop();
    const float level = out[static_cast<std::size_t>(c.cell)];
    core::Status status;
    num::for_each_neighbor(grid, c.cell, num::Connectivity::d8,
                           [&](std::int64_t j, core::Dir8 /*d*/) {
                             const auto ju = static_cast<std::size_t>(j);
                             if ((*closed)[ju] != 0) {
                               return;
                             }
                             (*closed)[ju] = 1;
                             float v = z[ju];
                             if (v <= level) {
                               v = epsilon > 0.0F ? raise_above(level, epsilon) : level;
                             }
                             out[ju] = v;
                             if (core::Status s = heap->push(v, j); !s.ok() && status.ok()) {
                               status = std::move(s);
                             }
                           });
    if (!status.ok()) {
      return status;
    }
  }
  return {};
}

}  // namespace

core::Status fill_depressions(std::span<const float> z, const core::Grid& grid,
                              std::span<float> water_level,
                              core::MemoryResource& resource) noexcept {
  return flood(z, grid, water_level, 0.0F,
               "fill_depressions: invalid grid, span size or non-finite z", resource);
}

core::Status priority_flood(std::span<const float> z, const core::Grid& grid,
                            std::span<float> z_filled, const PriorityFloodParams& params,
                            core::MemoryResource& resource) noexcept {
  if (!std::isfinite(params.epsilon) || !(params.epsilon > 0.0F)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "priority_flood: epsilon must be finite and > 0");
  }
  return flood(z, grid, z_filled, params.epsilon,
               "priority_flood: invalid grid, span size or non-finite z", resource);
}

}  // namespace wg::hydro
