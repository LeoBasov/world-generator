#include "wg/bio/salinity.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/detmath.hpp"
#include "wg/hydro/water.hpp"
#include "wg/numerics/distance.hpp"

namespace wg::bio {

namespace detail = num::detail;

namespace {

constexpr double kOceanSalinity = 100.0;

// Channel distance of every river node to its mouth and the mouth's node, by walking each node
// downstream to a node already done and unwinding the path. state: 0 new, 1 on the path, 2 done.
core::Status channel_distances(const core::RiverGraph& g, const core::Grid& grid,
                               std::span<double> length, std::span<std::int64_t> mouth,
                               std::span<std::uint8_t> state,
                               std::span<std::int64_t> path) noexcept {
  const auto nodes = static_cast<std::int64_t>(g.size());
  std::fill(state.begin(), state.end(), std::uint8_t{0});
  for (std::int64_t start = 0; start < nodes; ++start) {
    std::size_t depth = 0;
    std::int64_t v = start;
    while (v >= 0 && state[static_cast<std::size_t>(v)] == 0) {
      state[static_cast<std::size_t>(v)] = 1;
      path[depth++] = v;
      v = g.downstream[static_cast<std::size_t>(v)];
    }
    if (v >= 0 && state[static_cast<std::size_t>(v)] == 1) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "compute_salinity: river graph has a cycle");
    }
    while (depth > 0) {
      const std::int64_t u = path[--depth];
      const auto uu = static_cast<std::size_t>(u);
      const std::int64_t down = g.downstream[uu];
      if (down < 0) {
        length[uu] = 0.0;
        mouth[uu] = u;
      } else {
        const auto du = static_cast<std::size_t>(down);
        const std::int64_t a = g.cell[uu];
        const std::int64_t b = g.cell[du];
        const auto dx = static_cast<double>(grid.x_of(a) - grid.x_of(b));
        const auto dy = static_cast<double>(grid.y_of(a) - grid.y_of(b));
        length[uu] = length[du] + std::sqrt((dx * dx) + (dy * dy));
        mouth[uu] = mouth[du];
      }
      state[uu] = 2;
    }
  }
  return {};
}

core::Status apply_estuaries(const core::RiverGraph& g, const core::Grid& grid, double reach,
                             std::span<std::uint8_t> out, core::MemoryResource& resource) noexcept {
  if (!g.consistent()) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "compute_salinity: river graph is inconsistent");
  }
  const std::size_t nodes = g.size();
  for (std::size_t v = 0; v < nodes; ++v) {
    const std::int64_t down = g.downstream[v];
    if (!grid.contains(g.cell[v]) || down < -1 || down >= static_cast<std::int64_t>(nodes)) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "compute_salinity: river node out of range");
    }
  }
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<double>(nodes) + (2 * detail::arena_bytes<std::int64_t>(nodes)) +
          (2 * detail::arena_bytes<std::uint8_t>(nodes)),
      resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<double>> length = arena->allocate<double>(nodes);
  core::Expected<std::span<std::int64_t>> mouth = arena->allocate<std::int64_t>(nodes);
  core::Expected<std::span<std::int64_t>> path = arena->allocate<std::int64_t>(nodes);
  core::Expected<std::span<std::uint8_t>> state = arena->allocate<std::uint8_t>(nodes);
  core::Expected<std::span<std::uint8_t>> inherited = arena->allocate<std::uint8_t>(nodes);
  if (!length || !mouth || !path || !state || !inherited) {
    return core::make_status(core::StatusCode::out_of_memory, "compute_salinity: scratch");
  }
  if (core::Status s = channel_distances(g, grid, *length, *mouth, *state, *path); !s.ok()) {
    return s;
  }
  // Read every mouth class before any cell is raised.
  for (std::size_t v = 0; v < nodes; ++v) {
    const auto m = static_cast<std::size_t>((*mouth)[v]);
    (*inherited)[v] = out[static_cast<std::size_t>(g.cell[m])];
  }
  for (std::size_t v = 0; v < nodes; ++v) {
    if ((*length)[v] <= reach) {
      std::uint8_t& c = out[static_cast<std::size_t>(g.cell[v])];
      c = std::max(c, (*inherited)[v]);
    }
  }
  return {};
}

}  // namespace

double coastal_salinity(double distance, double decay_length) noexcept {
  if (!(distance < std::numeric_limits<double>::infinity())) {
    return 0.0;
  }
  return kOceanSalinity * core::det_exp(-distance / decay_length);
}

double fresh_distance(const params::SalinityClasses& classes, double decay_length) noexcept {
  if (classes.classes.size() < 2) {
    return 0.0;
  }
  const double s1 = classes.classes[1].min_salinity;
  if (s1 <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  if (s1 >= kOceanSalinity) {
    return 0.0;
  }
  return decay_length * core::det_log(kOceanSalinity / s1);
}

core::Status compute_salinity(const core::Grid& grid, const SalinityInputs& in,
                              const params::SalinityParams& p,
                              const params::SalinityClasses& classes, std::span<std::uint8_t> out,
                              core::Executor& exec, const core::CancelFlag* cancel,
                              core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.water, grid, "compute_salinity: water must have one value per cell"),
       detail::check_field(out, grid, "compute_salinity: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::uint8_t>(cells) + detail::arena_bytes<double>(cells), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::uint8_t>> ocean = arena->allocate<std::uint8_t>(cells);
  core::Expected<std::span<double>> d2 = arena->allocate<double>(cells);
  if (!ocean || !d2) {
    return core::make_status(core::StatusCode::out_of_memory, "compute_salinity: scratch");
  }
  const auto ocean_class = static_cast<std::uint8_t>(hydro::WaterClass::ocean);
  for (std::size_t i = 0; i < cells; ++i) {
    (*ocean)[i] = in.water[i] == ocean_class ? 1 : 0;
  }
  if (core::Status s = num::edt_squared(*ocean, grid, *d2, exec, resource); !s.ok()) {
    return s;
  }
  const std::int64_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      const double s =
          (*ocean)[k] != 0 ? kOceanSalinity : coastal_salinity(std::sqrt((*d2)[k]), p.decay_length);
      out[k] = static_cast<std::uint8_t>(classes.quantise(s));
    }
  };
  if (core::Status s = core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel); !s.ok()) {
    return s;
  }
  if (in.rivers == nullptr || in.rivers->empty()) {
    return {};
  }
  if (cancel != nullptr && cancel->requested()) {
    return core::make_status(core::StatusCode::cancelled, "compute_salinity: cancelled");
  }
  return apply_estuaries(*in.rivers, grid, fresh_distance(classes, p.decay_length), out, resource);
}

core::Status assign_lake_salinity(std::span<const std::uint8_t> salinity, const core::Grid& grid,
                                  std::span<core::Lake> lakes) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(salinity, grid,
                           "assign_lake_salinity: salinity must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  for (const core::Lake& lake : lakes) {
    for (const std::int64_t c : lake.cells) {
      if (!grid.contains(c)) {
        return core::make_status(core::StatusCode::invalid_argument,
                                 "assign_lake_salinity: lake cell out of range");
      }
    }
  }
  for (core::Lake& lake : lakes) {
    std::uint8_t c = 0;
    for (const std::int64_t cell : lake.cells) {
      c = std::max(c, salinity[static_cast<std::size_t>(cell)]);
    }
    lake.salinity_class = c;
  }
  return {};
}

}  // namespace wg::bio
