#include "wg/hydro/lakes.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/labeling.hpp"

namespace wg::hydro {

namespace detail = num::detail;

namespace {

core::Expected<std::vector<core::Lake>> bad(std::string_view message) noexcept {
  return core::make_error(core::StatusCode::invalid_argument, message);
}

// Per-component summary while scanning the cells.
struct Candidate {
  float level = 0.0F;
  double volume = 0.0;
  std::int64_t entry = core::kNoCell;  // cell with the lowest (z_filled, index)
  std::int32_t lake = -1;
};

}  // namespace

core::Expected<std::vector<core::Lake>> label_lakes(
    std::span<const float> z, std::span<const float> water_level, std::span<const float> z_filled,
    std::span<const std::int32_t> receivers, const core::Grid& grid, const LakeParams& params,
    std::span<std::int32_t> lake_id, core::MemoryResource& resource) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "label_lakes: z must have one value per cell"),
           detail::check_field(water_level, grid,
                               "label_lakes: water_level must have one value per cell"),
           detail::check_field(z_filled, grid,
                               "label_lakes: z_filled must have one value per cell"),
           detail::check_field(receivers, grid,
                               "label_lakes: receivers must have one value per cell"),
           detail::check_field(lake_id, grid,
                               "label_lakes: lake_id must have one value per cell")});
      !s.ok()) {
    return core::make_error(std::move(s));
  }
  if (!std::isfinite(params.min_volume) || params.min_volume < 0.0) {
    return bad("label_lakes: min_volume must be finite and >= 0");
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<std::uint8_t>(cells), resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<std::uint8_t>> wet = arena->allocate<std::uint8_t>(cells);
  if (!wet) {
    return core::make_error(std::move(wet.error()));
  }
  for (std::size_t i = 0; i < cells; ++i) {
    if (!(water_level[i] >= z[i])) {
      return bad("label_lakes: water_level lies below z");
    }
    (*wet)[i] = water_level[i] > z[i] ? 1 : 0;
  }
  // The labels go straight into lake_id and are replaced by lake ids below.
  core::Expected<std::int32_t> count =
      num::label_components(*wet, grid, num::Connectivity::d8, lake_id, resource);
  if (!count) {
    return core::make_error(std::move(count.error()));
  }
  // The vectors are the only allocations that can throw.
  try {
    std::vector<Candidate> candidates(static_cast<std::size_t>(*count) + 1);
    for (std::size_t i = 0; i < cells; ++i) {
      const std::int32_t label = lake_id[i];
      if (label == 0) {
        continue;
      }
      Candidate& c = candidates[static_cast<std::size_t>(label)];
      if (c.entry == core::kNoCell) {
        c.level = water_level[i];
        c.entry = static_cast<std::int64_t>(i);
      } else if (water_level[i] != c.level) {
        return bad("label_lakes: a depression has two water levels");
      } else if (z_filled[i] < z_filled[static_cast<std::size_t>(c.entry)]) {
        c.entry = static_cast<std::int64_t>(i);
      }
      c.volume += static_cast<double>(water_level[i]) - static_cast<double>(z[i]);
    }
    std::vector<core::Lake> lakes;
    for (std::size_t k = 1; k < candidates.size(); ++k) {
      Candidate& c = candidates[k];
      if (c.volume < params.min_volume) {
        continue;
      }
      const std::int32_t outlet = receivers[static_cast<std::size_t>(c.entry)];
      if (outlet < 0 || static_cast<std::size_t>(outlet) >= cells ||
          lake_id[static_cast<std::size_t>(outlet)] == static_cast<std::int32_t>(k)) {
        return bad("label_lakes: the outlet of a depression is not outside it");
      }
      c.lake = static_cast<std::int32_t>(lakes.size());
      core::Lake lake;
      lake.id = c.lake;
      lake.spill_elevation_eu = c.level;
      lake.outlet_cell = outlet;
      lakes.push_back(std::move(lake));
    }
    for (std::size_t i = 0; i < cells; ++i) {
      const std::int32_t label = lake_id[i];
      const std::int32_t lake = label == 0 ? -1 : candidates[static_cast<std::size_t>(label)].lake;
      lake_id[i] = lake;
      if (lake >= 0) {
        lakes[static_cast<std::size_t>(lake)].cells.push_back(static_cast<std::int64_t>(i));
      }
    }
    return lakes;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "label_lakes: lakes");
  }
}

}  // namespace wg::hydro
