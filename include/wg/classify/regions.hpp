#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/function_ref.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/params/tables.hpp"

namespace wg::classify {

// Regions of R9.3 (A20): the 8-connected components of equal region type. The conventions are
// recorded in docs/adr/0018-biomes-regions.md.

/// Naming hook (plan A20): returns the name id of a region, -1 for none.
using RegionNamer = core::FunctionRef<std::int32_t(const core::Region&)>;

/// Size class of a region of area cells: the index of the last entry of sizes whose min_tiles
/// is at most area (sizes ascending, as BiomeTable::region_sizes), 0 if there is none.
[[nodiscard]] std::uint8_t region_size_class(std::int64_t area,
                                             std::span<const params::RegionSize> sizes) noexcept;

/// Labels the regions: every maximal set of cells of equal region_type that is connected under
/// D8 adjacency is one region, so every cell belongs to exactly one. Regions are numbered 0 ...
/// K-1 in row-major order of their first cell, and region_id receives the number of each cell.
/// Each Region holds its type, area, centroid (mean column x and row y, from exact integer
/// sums), the ascending distinct values of biome on its cells, its size class
/// (region_size_class with sizes) and its name id from namer, or its id without one. Returns
/// the regions in id order. Errors: invalid_argument for an invalid grid or a span without one
/// value per cell; out_of_memory.
[[nodiscard]] core::Expected<std::vector<core::Region>> label_regions(
    std::span<const std::uint8_t> region_type, std::span<const params::BiomeId> biome,
    const core::Grid& grid, std::span<const params::RegionSize> sizes,
    std::span<std::int32_t> region_id, const RegionNamer* namer = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Cells and regions of one region type.
struct RegionTypeCount {
  std::int64_t cells = 0;
  std::int64_t regions = 0;

  friend bool operator==(const RegionTypeCount&, const RegionTypeCount&) = default;
};

/// Counts per region type 0 ... type_count-1: the cells of region_type with that value and the
/// regions of that type. Errors: invalid_argument for a region type or Region::type outside
/// [0, type_count); out_of_memory.
[[nodiscard]] core::Expected<std::vector<RegionTypeCount>> count_region_types(
    std::span<const std::uint8_t> region_type, std::span<const core::Region> regions,
    std::size_t type_count) noexcept;

}  // namespace wg::classify
