#include "wg/hydro/river_network.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/detmath.hpp"
#include "wg/hydro/discharge.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

core::Status bad(std::string_view message) noexcept {
  return core::make_status(core::StatusCode::invalid_argument, message);
}

std::size_t u(std::int64_t i) noexcept { return static_cast<std::size_t>(i); }

// Upstream nodes of every node in ascending index order (counting sort over downstream):
// the donors of node k are donors[start[k] ... start[k + 1]).
struct Donors {
  std::vector<std::int64_t> start;
  std::vector<std::int64_t> donors;
};

Donors donors_of(const core::RiverGraph& graph) {
  const std::size_t n = graph.size();
  Donors d{std::vector<std::int64_t>(n + 1, 0), std::vector<std::int64_t>(n)};
  for (std::size_t k = 0; k < n; ++k) {
    if (graph.downstream[k] >= 0) {
      ++d.start[u(graph.downstream[k]) + 1];
    }
  }
  for (std::size_t k = 0; k < n; ++k) {
    d.start[k + 1] += d.start[k];
  }
  std::vector<std::int64_t> cursor(d.start.begin(), d.start.end() - 1);
  for (std::size_t k = 0; k < n; ++k) {
    if (graph.downstream[k] >= 0) {
      d.donors[u(cursor[u(graph.downstream[k])]++)] = static_cast<std::int64_t>(k);
    }
  }
  return d;
}

}  // namespace

core::Expected<core::RiverGraph> trace_rivers(std::span<const std::int32_t> sources,
                                              std::span<const float> z,
                                              std::span<const std::int32_t> receivers,
                                              std::span<const std::int32_t> lake_id,
                                              std::span<const core::Lake> lakes,
                                              const core::Grid& grid,
                                              core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "trace_rivers: z must have one value per cell"),
           detail::check_field(receivers, grid,
                               "trace_rivers: receivers must have one value per cell"),
           detail::check_field(lake_id, grid,
                               "trace_rivers: lake_id must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  const std::int64_t cells = grid.cells();
  const auto lake_count = static_cast<std::int64_t>(lakes.size());
  for (std::size_t i = 0; i < u(cells); ++i) {
    if (receivers[i] < -1 || receivers[i] >= cells) {
      return core::make_error(bad("trace_rivers: a receiver is out of range"));
    }
    if (lake_id[i] < -1 || lake_id[i] >= lake_count) {
      return core::make_error(bad("trace_rivers: a lake id is out of range"));
    }
  }
  for (const std::int32_t s : sources) {
    if (s < 0 || s >= cells) {
      return core::make_error(bad("trace_rivers: a source is out of range"));
    }
  }
  for (const core::Lake& lake : lakes) {
    if (!grid.contains(lake.outlet_cell)) {
      return core::make_error(bad("trace_rivers: a lake outlet is out of range"));
    }
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<std::int32_t>(u(cells)), resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  // -2 for a cell off the rivers, -1 for a river cell before the nodes are numbered.
  core::Expected<std::span<std::int32_t>> node = arena->allocate<std::int32_t>(u(cells));
  if (!node) {
    return core::make_error(std::move(node.error()));
  }
  std::fill(node->begin(), node->end(), -2);
  try {
    std::vector<std::int64_t> work(sources.begin(), sources.end());
    std::vector<std::uint8_t> lake_reached(lakes.size(), 0);
    const auto reach_lake = [&](std::int32_t lake) {
      if (lake_reached[u(lake)] == 0) {
        lake_reached[u(lake)] = 1;
        work.push_back(lakes[u(lake)].outlet_cell);
      }
    };
    // The river cells are the union of all chains, so the order of the work list does not
    // matter. Each step marks a new cell, so the walks end.
    while (!work.empty()) {
      std::int64_t c = work.back();
      work.pop_back();
      if (z[u(c)] < terrain::kSeaLevel) {
        continue;
      }
      if (const std::int32_t lake = lake_id[u(c)]; lake >= 0) {
        reach_lake(lake);  // a start inside a lake continues from its outlet
        continue;
      }
      while ((*node)[u(c)] == -2) {
        (*node)[u(c)] = -1;
        const std::int64_t r = receivers[u(c)];
        if (r < 0 || z[u(r)] < terrain::kSeaLevel) {
          break;
        }
        if (const std::int32_t lake = lake_id[u(r)]; lake >= 0) {
          reach_lake(lake);
          break;
        }
        c = r;
      }
    }
    core::RiverGraph graph;
    std::int32_t count = 0;
    for (std::int64_t c = 0; c < cells; ++c) {
      if ((*node)[u(c)] == -1) {
        (*node)[u(c)] = count++;
        graph.cell.push_back(c);
      }
    }
    const auto n = u(count);
    graph.downstream.resize(n);
    graph.discharge_m3_yr.assign(n, 0.0F);
    graph.width_class.assign(n, 0);
    graph.order.assign(n, 0);
    graph.name_id.assign(n, -1);
    for (std::size_t k = 0; k < n; ++k) {
      const std::int32_t r = receivers[u(graph.cell[k])];
      graph.downstream[k] = r >= 0 && (*node)[u(r)] >= 0 ? (*node)[u(r)] : -1;
    }
    return graph;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "trace_rivers: river graph");
  }
}

core::Status check_river_graph(const core::RiverGraph& graph, std::span<const float> z,
                               std::span<const std::int32_t> receivers,
                               std::span<const std::int32_t> lake_id, const core::Grid& grid,
                               core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "check_river_graph: z must have one value per cell"),
           detail::check_field(receivers, grid,
                               "check_river_graph: receivers must have one value per cell"),
           detail::check_field(lake_id, grid,
                               "check_river_graph: lake_id must have one value per cell")});
      !s.ok()) {
    return s;
  }
  if (!graph.consistent()) {
    return bad("check_river_graph: the arrays differ in length");
  }
  const std::size_t n = graph.size();
  const auto cells = u(grid.cells());
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<std::uint8_t>(cells) + detail::arena_bytes<std::uint8_t>(n), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<std::uint8_t>> used = arena->allocate<std::uint8_t>(cells);
  core::Expected<std::span<std::uint8_t>> state = arena->allocate<std::uint8_t>(n);
  if (!used || !state) {
    return core::make_status(core::StatusCode::out_of_memory, "check_river_graph: scratch");
  }
  std::fill(used->begin(), used->end(), 0);
  std::fill(state->begin(), state->end(), 0);
  const auto ocean = [&](std::int64_t c) { return z[u(c)] < terrain::kSeaLevel; };
  for (std::size_t k = 0; k < n; ++k) {
    const std::int64_t c = graph.cell[k];
    if (!grid.contains(c)) {
      return bad("check_river_graph: a node lies outside the grid");
    }
    if ((*used)[u(c)] != 0) {
      return bad("check_river_graph: a cell carries two nodes");
    }
    (*used)[u(c)] = 1;
    if (ocean(c) || lake_id[u(c)] >= 0) {
      return bad("check_river_graph: a river cell lies in ocean or a lake");
    }
  }
  for (std::size_t k = 0; k < n; ++k) {
    const std::int64_t d = graph.downstream[k];
    const std::int64_t r = receivers[u(graph.cell[k])];
    if (d >= 0) {
      if (u(d) >= n) {
        return bad("check_river_graph: a downstream node is out of range");
      }
      if (r != graph.cell[u(d)]) {
        return bad("check_river_graph: a node does not drain to its receiver");
      }
    } else if (d != -1) {
      return bad("check_river_graph: a downstream node is out of range");
    } else if (r >= 0 &&
               (static_cast<std::size_t>(r) >= cells || (!ocean(r) && lake_id[u(r)] < 0))) {
      return bad("check_river_graph: a mouth drains onto land");
    }
  }
  // Follow each node downstream: 1 marks the current walk, 2 the nodes known to reach a mouth.
  for (std::size_t k = 0; k < n; ++k) {
    auto v = static_cast<std::int64_t>(k);
    while (v >= 0 && (*state)[u(v)] == 0) {
      (*state)[u(v)] = 1;
      v = graph.downstream[u(v)];
    }
    if (v >= 0 && (*state)[u(v)] == 1) {
      return bad("check_river_graph: the river graph contains a cycle");
    }
    for (v = static_cast<std::int64_t>(k); v >= 0 && (*state)[u(v)] == 1;
         v = graph.downstream[u(v)]) {
      (*state)[u(v)] = 2;
    }
  }
  return {};
}

core::Expected<WidthClassThresholds> width_class_thresholds(
    const params::RiversParams& params) noexcept {
  const double a = params.width_coefficient;
  const double b = params.width_exponent;
  if (!std::isfinite(a) || a < 0.0 || !std::isfinite(b) || b < 0.0) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "width_class_thresholds: a_w and b must be finite and >= 0");
  }
  WidthClassThresholds t{};
  for (std::size_t k = 0; k < t.size(); ++k) {
    const double w = params.width_class_bounds[k];
    if (!std::isfinite(w) || w < 0.0 || (k > 0 && !(params.width_class_bounds[k - 1] < w))) {
      return core::make_error(
          core::StatusCode::invalid_argument,
          "width_class_thresholds: bounds must be finite, >= 0 and strictly ascending");
    }
    if (w == 0.0) {
      t[k] = 0.0;
    } else if (a == 0.0 || b == 0.0) {
      // The width is constant: 0 without a coefficient, a_w for b = 0 (Q^0 = 1).
      const double constant = b == 0.0 ? a : 0.0;
      t[k] = w <= constant ? 0.0 : std::numeric_limits<double>::infinity();
    } else {
      // a_w (Q / s)^b >= w  <=>  Q >= s exp(ln(w / a_w) / b).
      t[k] = kSecondsPerYear * core::det_exp(core::det_log(w / a) / b);
    }
  }
  return t;
}

std::uint8_t river_width_class(double q, const WidthClassThresholds& thresholds) noexcept {
  std::uint8_t c = 1;
  for (const double t : thresholds) {
    if (q >= t) {
      ++c;
    }
  }
  return c;
}

core::Expected<std::vector<RiverStem>> river_attributes(core::RiverGraph& graph,
                                                        std::span<const double> discharge,
                                                        const core::Grid& grid,
                                                        const params::RiversParams& params,
                                                        const RiverNamer* namer) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(discharge, grid,
                               "river_attributes: discharge must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (!graph.consistent()) {
    return core::make_error(bad("river_attributes: the arrays differ in length"));
  }
  const std::size_t n = graph.size();
  for (std::size_t k = 0; k < n; ++k) {
    if (!grid.contains(graph.cell[k]) || graph.downstream[k] < -1 ||
        graph.downstream[k] >= static_cast<std::int64_t>(n)) {
      return core::make_error(bad("river_attributes: a cell or downstream node is out of range"));
    }
  }
  core::Expected<WidthClassThresholds> thresholds = width_class_thresholds(params);
  if (!thresholds) {
    return core::make_error(std::move(thresholds.error()));
  }
  try {
    const auto q = [&](std::int64_t k) { return discharge[u(graph.cell[u(k)])]; };
    const Donors d = donors_of(graph);
    // Main donor: the largest discharge, ties to the lower node (the donors ascend).
    std::vector<std::int64_t> main(n, -1);
    for (std::size_t k = 0; k < n; ++k) {
      for (std::int64_t j = d.start[k]; j < d.start[k + 1]; ++j) {
        const std::int64_t donor = d.donors[u(j)];
        if (main[k] < 0 || q(donor) > q(main[k])) {
          main[k] = donor;
        }
      }
    }
    // From the mouths upstream; each stem starts at a mouth or a tributary.
    std::vector<std::int32_t> stem(n, -1);
    std::vector<RiverStem> stems;
    std::vector<std::int64_t> queue;
    queue.reserve(n);
    const auto start_stem = [&](std::int64_t k, std::uint8_t order) {
      stem[u(k)] = static_cast<std::int32_t>(stems.size());
      RiverStem s;
      s.lowest_node = k;
      s.discharge_m3_yr = q(k);
      s.order = order;
      stems.push_back(s);
      graph.order[u(k)] = order;
      queue.push_back(k);
    };
    for (std::size_t k = 0; k < n; ++k) {
      if (graph.downstream[k] == -1) {
        start_stem(static_cast<std::int64_t>(k), 1);
      }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
      const std::int64_t k = queue[head];
      for (std::int64_t j = d.start[u(k)]; j < d.start[u(k) + 1]; ++j) {
        const std::int64_t donor = d.donors[u(j)];
        if (donor == main[u(k)]) {
          stem[u(donor)] = stem[u(k)];
          graph.order[u(donor)] = graph.order[u(k)];
          queue.push_back(donor);
        } else {
          const std::uint8_t order = graph.order[u(k)];
          start_stem(donor, order == 255 ? order : static_cast<std::uint8_t>(order + 1));
        }
      }
    }
    if (queue.size() != n) {
      return core::make_error(bad("river_attributes: the river graph contains a cycle"));
    }
    for (std::size_t k = 0; k < n; ++k) {
      RiverStem& s = stems[u(stem[k])];
      ++s.nodes;
      if (main[k] < 0) {
        s.head_node = static_cast<std::int64_t>(k);
      }
    }
    // Number the stems by descending discharge at the lowest node, ties to the lower cell.
    std::vector<std::int32_t> rank(stems.size());
    for (std::size_t s = 0; s < rank.size(); ++s) {
      rank[s] = static_cast<std::int32_t>(s);
    }
    std::sort(rank.begin(), rank.end(), [&](std::int32_t a, std::int32_t b) {
      const RiverStem& x = stems[u(a)];
      const RiverStem& y = stems[u(b)];
      if (x.discharge_m3_yr != y.discharge_m3_yr) {
        return x.discharge_m3_yr > y.discharge_m3_yr;
      }
      return graph.cell[u(x.lowest_node)] < graph.cell[u(y.lowest_node)];
    });
    std::vector<RiverStem> sorted(stems.size());
    std::vector<std::int32_t> id_of(stems.size());
    for (std::size_t s = 0; s < rank.size(); ++s) {
      id_of[u(rank[s])] = static_cast<std::int32_t>(s);
      sorted[s] = stems[u(rank[s])];
      sorted[s].id = static_cast<std::int32_t>(s);
    }
    std::vector<std::int32_t> name(sorted.size());
    for (std::size_t s = 0; s < sorted.size(); ++s) {
      name[s] = namer != nullptr ? (*namer)(sorted[s]) : sorted[s].id;
    }
    for (std::size_t k = 0; k < n; ++k) {
      const double qk = q(static_cast<std::int64_t>(k));
      graph.discharge_m3_yr[k] = static_cast<float>(qk);
      graph.width_class[k] = river_width_class(qk, *thresholds);
      graph.name_id[k] = name[u(id_of[u(stem[k])])];
    }
    return sorted;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "river_attributes: scratch");
  }
}

}  // namespace wg::hydro
