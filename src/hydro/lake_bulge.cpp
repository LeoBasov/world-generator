#include "wg/hydro/lake_bulge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/numerics/neighborhood.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

core::Expected<std::int32_t> bad(std::string_view message) noexcept {
  return core::make_error(core::StatusCode::invalid_argument, message);
}

std::size_t u(std::int64_t i) noexcept { return static_cast<std::size_t>(i); }

// Smallest float32 >= v.
float float_at_least(double v) noexcept {
  auto f = static_cast<float>(v);
  if (static_cast<double>(f) < v) {
    f = std::nextafter(f, std::numeric_limits<float>::infinity());
  }
  return f;
}

}  // namespace

core::Expected<std::int32_t> bulge_lakes(std::span<const float> z,
                                         std::span<const std::int32_t> receivers,
                                         const core::RiverGraph& rivers, const core::Grid& grid,
                                         const LakeBulgeParams& params, core::RngKey key,
                                         std::span<std::int32_t> lake_id,
                                         std::vector<core::Lake>& lakes) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "bulge_lakes: z must have one value per cell"),
           detail::check_field(receivers, grid,
                               "bulge_lakes: receivers must have one value per cell"),
           detail::check_field(lake_id, grid,
                               "bulge_lakes: lake_id must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (params.count < 0 || !std::isfinite(params.height) || params.height < 0.0) {
    return bad("bulge_lakes: count must be >= 0 and height finite and >= 0");
  }
  const std::int64_t cells = grid.cells();
  for (const core::Lake& lake : lakes) {
    if (lake.cells.empty()) {
      return bad("bulge_lakes: a lake has no cells");
    }
    for (const std::int64_t c : lake.cells) {
      if (!grid.contains(c)) {
        return bad("bulge_lakes: a lake cell is out of range");
      }
    }
  }
  if (!rivers.consistent()) {
    return bad("bulge_lakes: the river graph arrays differ in length");
  }
  for (std::size_t k = 0; k < rivers.size(); ++k) {
    if (!grid.contains(rivers.cell[k]) || rivers.downstream[k] < -1 ||
        rivers.downstream[k] >= static_cast<std::int64_t>(rivers.size())) {
      return bad("bulge_lakes: a river cell or downstream node is out of range");
    }
  }
  for (std::size_t i = 0; i < u(cells); ++i) {
    if (receivers[i] < -1 || receivers[i] >= cells) {
      return bad("bulge_lakes: a receiver is out of range");
    }
  }
  try {
    const auto ocean = [&](std::int64_t c) { return z[u(c)] < terrain::kSeaLevel; };
    const auto wet = [&](std::int64_t c) { return ocean(c) || lake_id[u(c)] >= 0; };
    std::vector<std::int64_t> candidates;
    for (std::size_t k = 0; k < rivers.size(); ++k) {
      const std::int64_t c = rivers.cell[k];
      bool dry = rivers.downstream[k] >= 0 && !wet(c);
      num::for_each_neighbor(grid, c, num::Connectivity::d8,
                             [&](std::int64_t j, core::Dir8) { dry = dry && !wet(j); });
      if (dry) {
        candidates.push_back(c);
      }
    }
    // Donors of every cell (counting sort over the receivers).
    std::vector<std::int64_t> start(u(cells) + 1, 0);
    std::vector<std::int64_t> donors(u(cells));
    for (std::size_t i = 0; i < u(cells); ++i) {
      if (receivers[i] >= 0) {
        ++start[u(receivers[i]) + 1];
      }
    }
    for (std::size_t i = 0; i < u(cells); ++i) {
      start[i + 1] += start[i];
    }
    {
      std::vector<std::int64_t> cursor(start.begin(), start.end() - 1);
      for (std::size_t i = 0; i < u(cells); ++i) {
        if (receivers[i] >= 0) {
          donors[u(cursor[u(receivers[i])]++)] = static_cast<std::int64_t>(i);
        }
      }
    }

    std::vector<std::int32_t> bulge(u(cells), -1);
    // True if a cell next to c is ocean, a lake or a bulge other than id.
    const auto crowded = [&](std::int64_t c, std::int32_t id) {
      bool near = false;
      num::for_each_neighbor(grid, c, num::Connectivity::d8, [&](std::int64_t j, core::Dir8) {
        near = near || wet(j) || (bulge[u(j)] >= 0 && bulge[u(j)] != id);
      });
      return near;
    };
    std::vector<core::Lake> merged = lakes;
    const auto m = static_cast<std::uint64_t>(candidates.size());
    const std::uint64_t picks = std::min(static_cast<std::uint64_t>(params.count), m);
    std::int32_t added = 0;
    std::vector<std::int64_t> queue;
    for (std::uint64_t k = 0; k < picks; ++k) {
      const std::uint64_t j = k + core::uniform_index(key, k, 0, m - k);
      std::swap(candidates[k], candidates[j]);
      const std::int64_t c = candidates[k];
      const float level = float_at_least(static_cast<double>(z[u(c)]) + params.height);
      if (bulge[u(c)] >= 0 || !(z[u(c)] < level)) {
        continue;
      }
      const std::int32_t id = added;
      if (crowded(c, id)) {
        continue;
      }
      queue.assign(1, c);
      bulge[u(c)] = id;
      for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::int64_t v = queue[head];
        for (std::int64_t q = start[u(v)]; q < start[u(v) + 1]; ++q) {
          const std::int64_t d = donors[u(q)];
          if (z[u(d)] < level && !wet(d) && bulge[u(d)] < 0 && !crowded(d, id)) {
            bulge[u(d)] = id;
            queue.push_back(d);
          }
        }
      }
      std::sort(queue.begin(), queue.end());
      core::Lake lake;
      lake.cells = queue;
      lake.spill_elevation_eu = level;
      lake.outlet_cell = receivers[u(c)];
      merged.push_back(std::move(lake));
      ++added;
    }
    std::stable_sort(merged.begin(), merged.end(), [](const core::Lake& a, const core::Lake& b) {
      return a.cells.front() < b.cells.front();
    });
    // Every allocation is done; commit.
    std::fill(lake_id.begin(), lake_id.end(), -1);
    for (std::size_t l = 0; l < merged.size(); ++l) {
      merged[l].id = static_cast<std::int32_t>(l);
      for (const std::int64_t c : merged[l].cells) {
        lake_id[u(c)] = static_cast<std::int32_t>(l);
      }
    }
    lakes = std::move(merged);
    return added;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "bulge_lakes: scratch");
  }
}

}  // namespace wg::hydro
