#include "wg/classify/regions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/numerics/labeling.hpp"

namespace wg::classify {

namespace {

namespace detail = num::detail;

// Neighbours that precede a cell in row-major order, i.e. visited before it.
constexpr std::array<core::Dir8, 4> kEarlier = {core::Dir8::w, core::Dir8::nw, core::Dir8::n,
                                                core::Dir8::ne};

}  // namespace

std::uint8_t region_size_class(std::int64_t area,
                               std::span<const params::RegionSize> sizes) noexcept {
  std::uint8_t size_class = 0;
  for (std::size_t k = 0; k < sizes.size(); ++k) {
    if (sizes[k].min_tiles <= area) {
      size_class = static_cast<std::uint8_t>(k);
    }
  }
  return size_class;
}

core::Expected<std::vector<core::Region>> label_regions(std::span<const std::uint8_t> region_type,
                                                        std::span<const params::BiomeId> biome,
                                                        const core::Grid& grid,
                                                        std::span<const params::RegionSize> sizes,
                                                        std::span<std::int32_t> region_id,
                                                        const RegionNamer* namer,
                                                        core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(region_type, grid,
                           "label_regions: region_type must have one value per cell"),
       detail::check_field(biome, grid, "label_regions: biome must have one value per cell"),
       detail::check_field(region_id, grid,
                           "label_regions: region_id must have one value per cell")});
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<num::UnionFind> sets = num::UnionFind::create(cells, resource);
  if (!sets) {
    return core::make_error(std::move(sets.error()));
  }
  // Pass 1: join every cell with its earlier neighbours of the same type.
  for (std::size_t i = 0; i < cells; ++i) {
    for (const core::Dir8 d : kEarlier) {
      const std::int64_t j = grid.neighbor(static_cast<std::int64_t>(i), d);
      if (j != core::kNoCell && region_type[static_cast<std::size_t>(j)] == region_type[i]) {
        sets->unite(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
      }
    }
  }
  try {
    // Pass 2: a root is its region's first cell in row-major order (UnionFind keeps the
    // smallest element as root), so regions are numbered when their root is reached.
    std::vector<core::Region> regions;
    std::vector<std::int64_t> sum_x;
    std::vector<std::int64_t> sum_y;
    for (std::size_t i = 0; i < cells; ++i) {
      const auto root = static_cast<std::size_t>(sets->find(static_cast<std::int32_t>(i)));
      if (root == i) {
        region_id[i] = static_cast<std::int32_t>(regions.size());
        core::Region& r = regions.emplace_back();
        r.id = region_id[i];
        r.type = region_type[i];
        sum_x.push_back(0);
        sum_y.push_back(0);
      } else {
        region_id[i] = region_id[root];
      }
      const auto id = static_cast<std::size_t>(region_id[i]);
      core::Region& r = regions[id];
      ++r.area_cells;
      sum_x[id] += grid.x_of(static_cast<std::int64_t>(i));
      sum_y[id] += grid.y_of(static_cast<std::int64_t>(i));
      const auto it = std::lower_bound(r.biomes.begin(), r.biomes.end(), biome[i]);
      if (it == r.biomes.end() || *it != biome[i]) {
        r.biomes.insert(it, biome[i]);
      }
    }
    for (std::size_t id = 0; id < regions.size(); ++id) {
      core::Region& r = regions[id];
      const auto area = static_cast<double>(r.area_cells);
      r.centroid_x = static_cast<double>(sum_x[id]) / area;
      r.centroid_y = static_cast<double>(sum_y[id]) / area;
      r.size_class = region_size_class(r.area_cells, sizes);
      r.name_id = namer != nullptr ? (*namer)(r) : r.id;
    }
    return regions;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "label_regions: no memory");
  }
}

core::Expected<std::vector<RegionTypeCount>> count_region_types(
    std::span<const std::uint8_t> region_type, std::span<const core::Region> regions,
    std::size_t type_count) noexcept {
  try {
    std::vector<RegionTypeCount> counts(type_count);
    for (const std::uint8_t t : region_type) {
      if (t >= type_count) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "count_region_types: region type out of range");
      }
      ++counts[t].cells;
    }
    for (const core::Region& r : regions) {
      if (r.type >= type_count) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "count_region_types: region type out of range");
      }
      ++counts[r.type].regions;
    }
    return counts;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "count_region_types: no memory");
  }
}

}  // namespace wg::classify
