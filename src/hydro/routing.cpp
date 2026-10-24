#include "wg/hydro/routing.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/neighborhood.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

core::Status bad(std::string_view message) noexcept {
  return core::make_status(core::StatusCode::invalid_argument, message);
}

// True if r is a D8 neighbour of i.
bool is_neighbor(const core::Grid& grid, std::int64_t i, std::int64_t r) noexcept {
  if (r == i || !grid.contains(r)) {
    return false;
  }
  return std::abs(grid.x_of(i) - grid.x_of(r)) <= 1 && std::abs(grid.y_of(i) - grid.y_of(r)) <= 1;
}

}  // namespace

core::Status d8_receivers(std::span<const float> z, const core::Grid& grid,
                          std::span<std::int32_t> receivers) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "d8_receivers: z must have one value per cell"),
           detail::check_field(receivers, grid,
                               "d8_receivers: receivers must have one value per cell")});
      !s.ok()) {
    return s;
  }
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const auto iu = static_cast<std::size_t>(i);
    std::int64_t best = core::kNoCell;
    if (!(z[iu] < terrain::kSeaLevel)) {
      const auto zi = static_cast<double>(z[iu]);
      double steepest = 0.0;
      num::for_each_neighbor(grid, i, num::Connectivity::d8, [&](std::int64_t j, core::Dir8 d) {
        const double drop =
            (zi - static_cast<double>(z[static_cast<std::size_t>(j)])) / num::step_length(d);
        if (drop > steepest) {
          steepest = drop;
          best = j;
        }
      });
    }
    receivers[iu] = static_cast<std::int32_t>(best);
  }
  return {};
}

core::Status stack_order(std::span<const std::int32_t> receivers, const core::Grid& grid,
                         std::span<std::int32_t> order, core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(receivers, grid,
                               "stack_order: receivers must have one value per cell"),
           detail::check_field(order, grid, "stack_order: order must have one value per cell")});
      !s.ok()) {
    return s;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  for (std::size_t i = 0; i < cells; ++i) {
    const std::int32_t r = receivers[i];
    if (r != -1 && !is_neighbor(grid, static_cast<std::int64_t>(i), r)) {
      return bad("stack_order: a receiver is neither -1 nor a D8 neighbour");
    }
  }
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::int32_t>(cells + 1) + (2 * detail::arena_bytes<std::int32_t>(cells)),
      resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::int32_t>> start = arena->allocate<std::int32_t>(cells + 1);
  core::Expected<std::span<std::int32_t>> donors = arena->allocate<std::int32_t>(cells);
  core::Expected<std::span<std::int32_t>> stack = arena->allocate<std::int32_t>(cells);
  if (!start || !donors || !stack) {
    return core::make_status(core::StatusCode::out_of_memory, "stack_order: scratch");
  }
  // Donors of each cell in ascending index order (counting sort).
  std::fill(start->begin(), start->end(), 0);
  for (std::size_t i = 0; i < cells; ++i) {
    if (receivers[i] >= 0) {
      ++(*start)[static_cast<std::size_t>(receivers[i]) + 1];
    }
  }
  for (std::size_t i = 0; i < cells; ++i) {
    (*start)[i + 1] += (*start)[i];
  }
  {
    std::span<std::int32_t> cursor = *stack;  // reused as the fill cursor
    std::copy(start->begin(), start->begin() + static_cast<std::ptrdiff_t>(cells), cursor.begin());
    for (std::size_t i = 0; i < cells; ++i) {
      const std::int32_t r = receivers[i];
      if (r >= 0) {
        (*donors)[static_cast<std::size_t>(cursor[static_cast<std::size_t>(r)]++)] =
            static_cast<std::int32_t>(i);
      }
    }
  }
  // Depth first from each sink; donors are pushed in reverse so the smallest comes out first.
  std::size_t written = 0;
  for (std::size_t s = 0; s < cells; ++s) {
    if (receivers[s] != -1) {
      continue;
    }
    std::size_t top = 0;
    (*stack)[top++] = static_cast<std::int32_t>(s);
    while (top > 0) {
      const auto c = static_cast<std::size_t>((*stack)[--top]);
      order[written++] = static_cast<std::int32_t>(c);
      for (std::int32_t k = (*start)[c + 1]; k > (*start)[c]; --k) {
        (*stack)[top++] = (*donors)[static_cast<std::size_t>(k) - 1];
      }
    }
  }
  // Cells on a cycle never drain into a sink.
  if (written != cells) {
    return bad("stack_order: the receivers contain a cycle");
  }
  return {};
}

core::Status accumulate_flow(std::span<const std::int32_t> receivers,
                             std::span<const std::int32_t> order, std::span<const double> weights,
                             const core::Grid& grid, std::span<double> accumulation) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(receivers, grid,
                               "accumulate_flow: receivers must have one value per cell"),
           detail::check_field(order, grid, "accumulate_flow: order must have one value per cell"),
           detail::check_field(weights, grid,
                               "accumulate_flow: weights must have one value per cell"),
           detail::check_field(accumulation, grid,
                               "accumulate_flow: accumulation must have one value per cell")});
      !s.ok()) {
    return s;
  }
  const std::int64_t cells = grid.cells();
  for (std::size_t k = 0; k < order.size(); ++k) {
    if (order[k] < 0 || order[k] >= cells || receivers[k] < -1 || receivers[k] >= cells) {
      return bad("accumulate_flow: index out of range");
    }
  }
  std::copy(weights.begin(), weights.end(), accumulation.begin());
  for (std::size_t k = order.size(); k > 0; --k) {
    const auto i = static_cast<std::size_t>(order[k - 1]);
    const std::int32_t r = receivers[i];
    if (r >= 0) {
      accumulation[static_cast<std::size_t>(r)] += accumulation[i];
    }
  }
  return {};
}

}  // namespace wg::hydro
