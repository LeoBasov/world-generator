#include "wg/terrain/river_descent.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"

namespace wg::terrain {

namespace detail = num::detail;

namespace {

core::Status bad(std::string_view message) noexcept {
  return core::make_status(core::StatusCode::invalid_argument, message);
}

}  // namespace

core::Status enforce_river_descent(std::span<float> z, const core::Grid& grid,
                                   const core::RiverGraph& rivers, double min_drop, float max_z,
                                   core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "enforce_river_descent: z must have one value per cell")});
      !s.ok()) {
    return s;
  }
  if (!std::isfinite(min_drop) || min_drop < 0.0) {
    return bad("enforce_river_descent: min_drop must be finite and >= 0");
  }
  if (!rivers.consistent()) {
    return bad("enforce_river_descent: river graph arrays differ in length");
  }
  const std::size_t m = rivers.size();
  if (m == 0) {
    return {};
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::uint8_t>(cells) + (3 * detail::arena_bytes<std::int64_t>(m + 1)),
      resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::uint8_t>> used = arena->allocate<std::uint8_t>(cells);
  core::Expected<std::span<std::int64_t>> start = arena->allocate<std::int64_t>(m + 1);
  core::Expected<std::span<std::int64_t>> children = arena->allocate<std::int64_t>(m + 1);
  core::Expected<std::span<std::int64_t>> queue = arena->allocate<std::int64_t>(m + 1);
  if (!used || !start || !children || !queue) {
    return core::make_status(core::StatusCode::out_of_memory, "enforce_river_descent: scratch");
  }
  std::fill(used->begin(), used->end(), std::uint8_t{0});
  std::fill(start->begin(), start->end(), std::int64_t{0});

  const auto nodes = static_cast<std::int64_t>(m);
  for (std::size_t u = 0; u < m; ++u) {
    const std::int64_t c = rivers.cell[u];
    if (!grid.contains(c)) {
      return bad("enforce_river_descent: node cell outside the grid");
    }
    if ((*used)[static_cast<std::size_t>(c)] != 0) {
      return bad("enforce_river_descent: a cell holds two river nodes");
    }
    (*used)[static_cast<std::size_t>(c)] = 1;
    const std::int64_t d = rivers.downstream[u];
    if (d < -1 || d >= nodes || d == static_cast<std::int64_t>(u)) {
      return bad("enforce_river_descent: downstream index out of range");
    }
    if (d >= 0) {
      ++(*start)[static_cast<std::size_t>(d) + 1];
    }
  }
  // Children of each node in node order (counting sort), then breadth first from the mouths.
  for (std::size_t u = 0; u < m; ++u) {
    (*start)[u + 1] += (*start)[u];
  }
  {
    std::span<std::int64_t> cursor = *queue;  // reused as the fill cursor
    std::copy(start->begin(), start->begin() + static_cast<std::ptrdiff_t>(m), cursor.begin());
    for (std::size_t u = 0; u < m; ++u) {
      const std::int64_t d = rivers.downstream[u];
      if (d >= 0) {
        (*children)[static_cast<std::size_t>(cursor[static_cast<std::size_t>(d)]++)] =
            static_cast<std::int64_t>(u);
      }
    }
  }
  std::size_t tail = 0;
  for (std::size_t u = 0; u < m; ++u) {
    if (rivers.downstream[u] == -1) {
      (*queue)[tail++] = static_cast<std::int64_t>(u);
    }
  }
  for (std::size_t head = 0; head < tail; ++head) {
    const auto u = static_cast<std::size_t>((*queue)[head]);
    for (std::int64_t k = (*start)[u]; k < (*start)[u + 1]; ++k) {
      (*queue)[tail++] = (*children)[static_cast<std::size_t>(k)];
    }
  }
  // Nodes on a cycle are never reached from a mouth.
  if (tail != m) {
    return bad("enforce_river_descent: the river graph has a cycle");
  }
  // Every node comes after its downstream node in the queue.
  for (std::size_t head = 0; head < m; ++head) {
    const auto v = static_cast<std::size_t>((*queue)[head]);
    const std::int64_t d = rivers.downstream[v];
    if (d < 0) {
      continue;
    }
    const double target =
        static_cast<double>(z[static_cast<std::size_t>(rivers.cell[static_cast<std::size_t>(d)])]) +
        min_drop;
    float& zv = z[static_cast<std::size_t>(rivers.cell[v])];
    if (static_cast<double>(zv) < target) {
      auto raised = static_cast<float>(target);
      if (static_cast<double>(raised) < target) {
        raised = std::nextafter(raised, std::numeric_limits<float>::infinity());
      }
      if (raised > max_z) {
        return core::make_status(core::StatusCode::out_of_range,
                                 "enforce_river_descent: a river cell would rise above max_z");
      }
      zv = raised;
    }
  }
  return {};
}

}  // namespace wg::terrain
