#include "wg/hydro/thermal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/hydro/test_rivers.hpp"

namespace wg::hydro {

namespace detail = num::detail;

core::Expected<std::int64_t> thermal_erode(std::span<float> z, const core::Grid& grid,
                                           const ThermalParams& params,
                                           core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "thermal_erode: z must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (!std::isfinite(params.talus_threshold) || params.talus_threshold < 0.0 ||
      !(params.fraction >= 0.0 && params.fraction <= 1.0) || params.max_transfers < 1) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "thermal_erode: talus_threshold must be finite and >= 0, fraction in "
                            "[0, 1], max_transfers >= 1");
  }
  if (!std::all_of(z.begin(), z.end(),
                   [](float v) { return on_lattice(static_cast<double>(v)); })) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "thermal_erode: z must lie on the erosion lattice");
  }
  if (params.fraction == 0.0) {
    return 0;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      (2 * detail::arena_bytes<std::int32_t>(cells)) + detail::arena_bytes<std::uint8_t>(cells),
      resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<std::int32_t>> units = arena->allocate<std::int32_t>(cells);
  core::Expected<std::span<std::int32_t>> ring = arena->allocate<std::int32_t>(cells);
  core::Expected<std::span<std::uint8_t>> queued = arena->allocate<std::uint8_t>(cells);
  if (!units || !ring || !queued) {
    return core::make_error(core::StatusCode::out_of_memory, "thermal_erode: scratch");
  }
  // Work in whole quanta: |z| < 512 = 2^24 quanta fits int32 and converts back exactly.
  std::span<std::int32_t> u = *units;
  for (std::size_t i = 0; i < cells; ++i) {
    u[i] = static_cast<std::int32_t>(static_cast<double>(z[i]) / kErosionQuantum);
  }
  const std::int64_t theta = std::max<std::int64_t>(
      static_cast<std::int64_t>(std::floor(params.talus_threshold / kErosionQuantum)), 1);
  const double half_c = params.fraction / 2.0;
  const auto steep = [&](std::int64_t i, std::int64_t j) {
    return std::abs(static_cast<std::int64_t>(u[static_cast<std::size_t>(i)]) -
                    u[static_cast<std::size_t>(j)]) > theta;
  };

  // FIFO work list of cells whose pairs may exceed Theta, each queued at most once. It starts
  // with every such cell in row-major order; a cell whose value changes is queued again.
  std::span<std::uint8_t> in_queue = *queued;
  std::fill(in_queue.begin(), in_queue.end(), std::uint8_t{0});
  std::size_t head = 0;
  std::size_t size = 0;
  const auto push = [&](std::int64_t c) {
    const auto cu = static_cast<std::size_t>(c);
    if (in_queue[cu] == 0) {
      in_queue[cu] = 1;
      (*ring)[(head + size) % cells] = static_cast<std::int32_t>(c);
      ++size;
    }
  };
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    for (const core::Dir8 d : core::kDir8) {
      const std::int64_t j = grid.neighbor(i, d);
      if (j != core::kNoCell && steep(i, j)) {
        push(i);
        break;
      }
    }
  }

  std::int64_t transfers = 0;
  while (size > 0) {
    const std::int64_t i = (*ring)[head];
    head = (head + 1) % cells;
    --size;
    in_queue[static_cast<std::size_t>(i)] = 0;
    bool changed = false;
    for (const core::Dir8 dir : core::kDir8) {
      const std::int64_t j = grid.neighbor(i, dir);
      if (j == core::kNoCell || !steep(i, j)) {
        continue;
      }
      if (transfers == params.max_transfers) {
        return core::make_error(core::StatusCode::internal,
                                "thermal_erode: no convergence within max_transfers");
      }
      std::int32_t& a = u[static_cast<std::size_t>(i)];
      std::int32_t& b = u[static_cast<std::size_t>(j)];
      const std::int64_t delta = std::abs(static_cast<std::int64_t>(a) - b);
      const auto wanted =
          static_cast<std::int64_t>(std::ceil(half_c * static_cast<double>(delta - theta)));
      const auto d = static_cast<std::int32_t>(std::clamp<std::int64_t>(wanted, 1, delta / 2));
      if (a > b) {
        a -= d;
        b += d;
      } else {
        a += d;
        b -= d;
      }
      ++transfers;
      changed = true;
      push(j);
    }
    if (changed) {
      push(i);
    }
  }
  for (std::size_t i = 0; i < cells; ++i) {
    z[i] = static_cast<float>(static_cast<double>(u[i]) * kErosionQuantum);
  }
  return transfers;
}

}  // namespace wg::hydro
