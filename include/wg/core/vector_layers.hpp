#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "wg/core/grid.hpp"

namespace wg::core {

// Vector layers of plan section 4.2: everything a stage produces that is not a raster field.
// Cells are cell indices (y * n + x) of the region-tile grid. Kinds that come from data tables
// (region type, rock, soil, mineral) are table ids. The producing work packages (A11 onwards)
// may extend these structs before 1.0; the world file stores them as JSON (A09).

/// Peak bump placed by the fractal stage (C2, P1).
struct Peak {
  std::int64_t cell = kNoCell;
  /// Peak elevation [elevation units].
  float height_eu = 0.0F;
  /// Radius l of the bump [cells].
  float radius_cells = 0.0F;

  friend bool operator==(const Peak&, const Peak&) = default;
};

/// Volcano placed by terrain shaping (C5, P1).
struct Volcano {
  std::int64_t cell = kNoCell;
  bool magma_connected = false;

  friend bool operator==(const Volcano&, const Volcano&) = default;
};

/// River network (C6, P6): a forest of nodes with downstream edges, stored as a struct of
/// arrays indexed by node. All arrays have the same length.
struct RiverGraph {
  /// Cell of each node.
  std::vector<std::int64_t> cell;
  /// Downstream node index, -1 at a mouth.
  std::vector<std::int64_t> downstream;
  /// Discharge Q at the node.
  std::vector<float> discharge_m3_yr;
  /// River width class.
  std::vector<std::uint8_t> width_class;
  /// Tributary (Strahler) order.
  std::vector<std::uint8_t> order;
  /// River name, -1 if unnamed.
  std::vector<std::int32_t> name_id;

  [[nodiscard]] std::size_t size() const noexcept { return cell.size(); }
  [[nodiscard]] bool empty() const noexcept { return cell.empty(); }
  /// True if every array has one entry per node.
  [[nodiscard]] bool consistent() const noexcept {
    const std::size_t n = cell.size();
    return downstream.size() == n && discharge_m3_yr.size() == n && width_class.size() == n &&
           order.size() == n && name_id.size() == n;
  }
  void clear() noexcept {
    cell.clear();
    downstream.clear();
    discharge_m3_yr.clear();
    width_class.clear();
    order.clear();
    name_id.clear();
  }

  friend bool operator==(const RiverGraph&, const RiverGraph&) = default;
};

/// Lake (C6, P4); id matches the lake_id field.
struct Lake {
  std::int32_t id = 0;
  std::vector<std::int64_t> cells;
  float spill_elevation_eu = 0.0F;
  std::int64_t outlet_cell = kNoCell;
  std::uint8_t salinity_class = 0;

  friend bool operator==(const Lake&, const Lake&) = default;
};

/// Named region (C9, P10); id matches the region_id field.
struct Region {
  std::int32_t id = 0;
  /// Region type (table id).
  std::uint16_t type = 0;
  std::int64_t area_cells = 0;
  /// Centroid [cells].
  double centroid_x = 0.0;
  double centroid_y = 0.0;
  /// Fine biomes present (biome field values), ascending.
  std::vector<std::uint16_t> biomes;
  std::uint8_t size_class = 0;
  std::int32_t name_id = -1;

  friend bool operator==(const Region&, const Region&) = default;
};

enum class PlateType : std::uint8_t { continental, oceanic };
enum class BoundaryClass : std::uint8_t { convergent, divergent, transform };

/// Boundary segment between two plates; rates as defined in plan section 7.2 (A24).
struct PlateBoundary {
  std::int32_t other_plate = -1;
  BoundaryClass boundary_class = BoundaryClass::transform;
  std::vector<std::int64_t> cells;
  /// Convergence rate c = dv . n.
  double convergence = 0.0;
  /// Shear rate s = |dv x n|.
  double shear = 0.0;

  friend bool operator==(const PlateBoundary&, const PlateBoundary&) = default;
};

/// Tectonic plate (P1); id matches the plate_id field.
struct Plate {
  std::int32_t id = 0;
  PlateType type = PlateType::continental;
  /// Centroid c_k [cells].
  double centroid_x = 0.0;
  double centroid_y = 0.0;
  /// Translation t_k.
  double translation_x = 0.0;
  double translation_y = 0.0;
  /// Angular velocity w_k.
  double omega = 0.0;
  std::vector<PlateBoundary> boundaries;

  friend bool operator==(const Plate&, const Plate&) = default;
};

struct RockLayer {
  /// Rock table id.
  std::uint16_t rock_id = 0;
  float thickness_m = 0.0F;

  friend bool operator==(const RockLayer&, const RockLayer&) = default;
};

struct SoilLayer {
  /// Soil table id.
  std::uint16_t soil_id = 0;
  float thickness_m = 0.0F;

  friend bool operator==(const SoilLayer&, const SoilLayer&) = default;
};

/// Stone column of one geological region (C10, P3), top to bottom; geo_region matches the
/// geo_region field.
struct GeoColumn {
  std::int32_t geo_region = 0;
  std::vector<RockLayer> layers;
  std::vector<SoilLayer> soil_layers;
  /// Cavern layers present, ascending.
  std::vector<std::uint8_t> cavern_levels;

  friend bool operator==(const GeoColumn&, const GeoColumn&) = default;
};

enum class InclusionType : std::uint8_t { vein, cluster, cluster_small, cluster_one };

/// Mineral deposit (C10, P8).
struct MineralDeposit {
  /// Mineral table id.
  std::uint16_t mineral_id = 0;
  /// Index into the GeoColumn layers of the cell's region.
  std::uint16_t host_layer = 0;
  InclusionType inclusion = InclusionType::vein;
  std::int64_t cell = kNoCell;

  friend bool operator==(const MineralDeposit&, const MineralDeposit&) = default;
};

/// All vector layers of a world.
struct VectorLayers {
  std::vector<Peak> peaks;
  std::vector<Volcano> volcanoes;
  RiverGraph river_graph;
  std::vector<Lake> lakes;
  std::vector<Region> regions;
  std::vector<Plate> plates;
  std::vector<GeoColumn> geo_columns;
  std::vector<MineralDeposit> minerals;

  [[nodiscard]] bool empty() const noexcept {
    return peaks.empty() && volcanoes.empty() && river_graph.empty() && lakes.empty() &&
           regions.empty() && plates.empty() && geo_columns.empty() && minerals.empty();
  }
  void clear() noexcept {
    peaks.clear();
    volcanoes.clear();
    river_graph.clear();
    lakes.clear();
    regions.clear();
    plates.clear();
    geo_columns.clear();
    minerals.clear();
  }

  friend bool operator==(const VectorLayers&, const VectorLayers&) = default;
};

}  // namespace wg::core
