#include "wg/hydro/test_rivers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

// Open cell of the flood from a pit, with the node it was reached from.
struct FloodEntry {
  float z = 0.0F;
  std::int64_t cell = core::kNoCell;
  std::int32_t parent = -1;
};

// Min-heap order: lowest z first, ties to the lower index.
bool later(const FloodEntry& a, const FloodEntry& b) noexcept {
  return a.z != b.z ? a.z > b.z : a.cell > b.cell;
}

// Flooded cell and its parent in the flood tree.
struct FloodNode {
  std::int64_t cell = core::kNoCell;
  std::int32_t parent = -1;
};

// Lowers v by depth, but not below sea level and never raises it.
void dig(float& v, double depth) noexcept {
  v = std::min(v, static_cast<float>(std::max(static_cast<double>(v) - depth,
                                              static_cast<double>(terrain::kSeaLevel))));
}

}  // namespace

double snap_to_lattice(double v) noexcept {
  return std::nearbyint(v / kErosionQuantum) * kErosionQuantum;
}

bool on_lattice(double v) noexcept {
  if (!std::isfinite(v) || std::abs(v) >= kLatticeLimit) {
    return false;
  }
  const double scaled = v / kErosionQuantum;  // exact: the quantum is a power of two
  return scaled == std::floor(scaled);
}

core::Status snap_to_lattice(std::span<float> z) noexcept {
  for (const float v : z) {
    if (!std::isfinite(v) || std::abs(static_cast<double>(v)) >= kLatticeLimit) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "snap_to_lattice: values must be finite and below 512 in magnitude");
    }
  }
  for (float& v : z) {
    v = static_cast<float>(snap_to_lattice(static_cast<double>(v)));
  }
  return {};
}

core::Expected<std::vector<std::int32_t>> find_river_sources(std::span<const float> z,
                                                             const core::Grid& grid) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "find_river_sources: z must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  try {
    std::vector<std::int32_t> sources;
    for (std::int64_t i = 0; i < grid.cells(); ++i) {
      if (!(z[static_cast<std::size_t>(i)] >= terrain::kMountainElevation)) {
        continue;
      }
      for (const core::Dir8 d : core::kDir8) {
        const std::int64_t j = grid.neighbor(i, d);
        if (j != core::kNoCell && z[static_cast<std::size_t>(j)] < terrain::kMountainElevation) {
          sources.push_back(static_cast<std::int32_t>(i));
          break;
        }
      }
    }
    return sources;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "find_river_sources: sources");
  }
}

core::Expected<WalkerResult> run_walker(std::span<float> z, const core::Grid& grid,
                                        std::int32_t source, const WalkerParams& params,
                                        core::RngStream& stream, std::span<std::uint8_t> visited,
                                        std::span<const std::uint8_t> stop_mask,
                                        std::vector<std::int32_t>* path) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "run_walker: z must have one value per cell"),
           detail::check_field(visited, grid, "run_walker: visited must have one value per cell"),
           detail::check_optional_field(stop_mask, grid,
                                        "run_walker: stop_mask must be empty or have one value "
                                        "per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (!grid.contains(static_cast<std::int64_t>(source))) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "run_walker: source lies outside the grid");
  }
  if (!std::isfinite(params.dig_depth) || params.dig_depth < 0.0 || params.step_limit < 0) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "run_walker: dig_depth must be finite and >= 0, step_limit >= 0");
  }
  WalkerResult result;
  try {
    std::vector<std::int32_t> local;
    std::vector<std::int32_t>& cells = path != nullptr ? *path : local;
    // Every cell marked in visited (1: walked or flooded, 2: queued), to clear at the end.
    std::vector<std::int64_t> touched;
    const auto mark = [&](std::int64_t c, std::uint8_t v) {
      if (visited[static_cast<std::size_t>(c)] == 0) {
        touched.push_back(c);
      }
      visited[static_cast<std::size_t>(c)] = v;
    };
    cells.clear();
    cells.push_back(source);
    mark(source, 1);
    std::int64_t i = source;
    std::array<std::int64_t, 8> lowest{};
    std::vector<FloodEntry> heap;
    std::vector<FloodNode> nodes;
    std::vector<std::int32_t> route;
    while (true) {
      const float zi = z[static_cast<std::size_t>(i)];
      if (zi < terrain::kSeaLevel) {
        result.stop = WalkerStop::ocean;
        break;
      }
      if (!stop_mask.empty() && stop_mask[static_cast<std::size_t>(i)] != 0) {
        result.stop = WalkerStop::mask;
        break;
      }
      if (result.steps == params.step_limit) {
        result.stop = WalkerStop::step_limit;
        break;
      }
      // The lowest neighbours not visited yet, in kDir8 order.
      std::size_t ties = 0;
      float low = 0.0F;
      for (const core::Dir8 d : core::kDir8) {
        const std::int64_t j = grid.neighbor(i, d);
        if (j == core::kNoCell || visited[static_cast<std::size_t>(j)] != 0) {
          continue;
        }
        const float zj = z[static_cast<std::size_t>(j)];
        if (ties == 0 || zj < low) {
          low = zj;
          ties = 0;
        }
        if (zj == low) {
          lowest[ties++] = j;
        }
      }
      if (ties == 0) {
        result.stop = WalkerStop::stuck;
        break;
      }
      if (low < zi) {
        std::size_t pick = 0;
        if (ties > 1) {
          pick = std::min(static_cast<std::size_t>(stream.uniform01() * static_cast<double>(ties)),
                          ties - 1);
        }
        i = lowest[pick];
        ++result.steps;
        cells.push_back(static_cast<std::int32_t>(i));
        mark(i, 1);
        continue;
      }

      // No lower neighbour: flood the depression from i in (z, index) order until a cell below
      // the water level, the ocean or the stop mask is reached.
      heap.clear();
      nodes.clear();
      nodes.push_back({i, -1});
      const auto enqueue = [&](std::int64_t c, std::int32_t parent) {
        for (const core::Dir8 d : core::kDir8) {
          const std::int64_t j = grid.neighbor(c, d);
          if (j != core::kNoCell && visited[static_cast<std::size_t>(j)] == 0) {
            mark(j, 2);
            heap.push_back({z[static_cast<std::size_t>(j)], j, parent});
            std::push_heap(heap.begin(), heap.end(), later);
          }
        }
      };
      enqueue(i, 0);
      float level = zi;
      std::int32_t exit = -1;
      bool spill = false;
      while (true) {
        if (heap.empty()) {
          result.stop = WalkerStop::stuck;
          break;
        }
        if (result.steps == params.step_limit) {
          result.stop = WalkerStop::step_limit;
          break;
        }
        std::pop_heap(heap.begin(), heap.end(), later);
        const FloodEntry e = heap.back();
        heap.pop_back();
        ++result.steps;
        const auto k = static_cast<std::int32_t>(nodes.size());
        nodes.push_back({e.cell, e.parent});
        mark(e.cell, 1);
        spill = e.z < level;
        if (spill || (!stop_mask.empty() && stop_mask[static_cast<std::size_t>(e.cell)] != 0)) {
          exit = k;
          break;
        }
        level = std::max(level, e.z);
        enqueue(e.cell, k);
      }
      if (exit < 0) {
        break;
      }
      // Cells queued but not flooded stay open for the rest of the walk.
      for (const FloodEntry& e : heap) {
        visited[static_cast<std::size_t>(e.cell)] = 0;
      }
      // The route from i to the exit along the flood tree; its highest cell is the saddle.
      route.clear();
      for (std::int32_t k = exit; k > 0; k = nodes[static_cast<std::size_t>(k)].parent) {
        route.push_back(k);
      }
      std::reverse(route.begin(), route.end());
      if (spill) {
        std::int64_t saddle = core::kNoCell;
        for (std::size_t r = 0; r + 1 < route.size(); ++r) {
          const std::int64_t c = nodes[static_cast<std::size_t>(route[r])].cell;
          if (saddle == core::kNoCell ||
              z[static_cast<std::size_t>(c)] > z[static_cast<std::size_t>(saddle)]) {
            saddle = c;
          }
        }
        if (saddle != core::kNoCell) {
          dig(z[static_cast<std::size_t>(saddle)], params.dig_depth);
        }
      }
      for (const std::int32_t k : route) {
        cells.push_back(static_cast<std::int32_t>(nodes[static_cast<std::size_t>(k)].cell));
      }
      i = nodes[static_cast<std::size_t>(exit)].cell;
    }
    result.end = static_cast<std::int32_t>(i);
    for (const std::int64_t c : touched) {
      visited[static_cast<std::size_t>(c)] = 0;
    }
  } catch (...) {
    // The list of marked cells may be incomplete, so the whole scratch is cleared.
    std::fill(visited.begin(), visited.end(), std::uint8_t{0});
    return core::make_error(core::StatusCode::out_of_memory, "run_walker: scratch");
  }
  return result;
}

}  // namespace wg::hydro
