#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/params/tables.hpp"

namespace wg::geology {

// Geological regions of R10 (A21): the areas that share one stone column. The conventions are
// recorded in docs/adr/0019-geology-subsurface.md.

/// Surface setting of a cell, which selects the eligible sedimentary rocks and soils.
enum class GeoEnvironment : std::uint8_t { deep_ocean, shallow_ocean, land, sand_desert, mountain };

/// Number of GeoEnvironment values.
inline constexpr std::size_t kGeoEnvironmentCount = 5;

/// True for deep and shallow ocean.
constexpr bool is_ocean(GeoEnvironment e) noexcept {
  return e == GeoEnvironment::deep_ocean || e == GeoEnvironment::shallow_ocean;
}

/// Width of a volcanism band; regions do not span bands.
inline constexpr double kVolcanismBandWidth = 20.0;
/// Number of volcanism bands: [0, 20), [20, 40), ..., [80, 100].
inline constexpr std::uint8_t kVolcanismBands = 5;

/// Band min(4, floor(V / 20)) of a volcanism value; 0 for V < 0 or NaN.
[[nodiscard]] std::uint8_t volcanism_band(float volcanism) noexcept;

/// Parameters of classify_environment.
struct EnvironmentParams {
  /// Ocean below this elevation is deep (geology.deep_ocean_elevation).
  double deep_ocean_elevation = 50.0;
  /// Biome of mountain cells (BiomeTable::mountain).
  params::BiomeId mountain_biome = 0;
  /// Biome of sand deserts, if the table has one.
  bool has_sand_desert = false;
  params::BiomeId sand_desert_biome = 0;
};

/// Environment of every cell, stored as GeoEnvironment values: deep ocean below
/// deep_ocean_elevation, shallow ocean below sea level (100), then mountain and sand desert by
/// biome, land otherwise (lakes included). Errors: invalid_argument for an invalid grid or a span
/// without one value per cell; cancelled.
[[nodiscard]] core::Status classify_environment(
    std::span<const float> elevation, std::span<const params::BiomeId> biome,
    const core::Grid& grid, const EnvironmentParams& params, std::span<std::uint8_t> environment,
    core::Executor& exec, const core::CancelFlag* cancel = nullptr) noexcept;

/// One geological region.
struct GeoRegion {
  std::int32_t id = 0;
  GeoEnvironment environment = GeoEnvironment::land;
  std::uint8_t volcanism_band = 0;
  std::int64_t area_cells = 0;
  /// Mean volcanism of the region's cells (float64 sum in cell order).
  double mean_volcanism = 0.0;

  friend bool operator==(const GeoRegion&, const GeoRegion&) = default;
};

/// The regions and their cells: the cells of region r are cells[offsets[r] ... offsets[r+1]),
/// ascending.
struct GeoRegions {
  std::vector<GeoRegion> regions;
  std::vector<std::int64_t> offsets;
  std::vector<std::int64_t> cells;

  [[nodiscard]] std::size_t size() const noexcept { return regions.size(); }
  [[nodiscard]] std::span<const std::int64_t> cells_of(std::size_t r) const noexcept {
    return std::span<const std::int64_t>(cells).subspan(
        static_cast<std::size_t>(offsets[r]),
        static_cast<std::size_t>(offsets[r + 1] - offsets[r]));
  }
};

/// Labels the geological regions: every maximal D8-connected set of cells with equal
/// environment and volcanism band is one region, so every cell belongs to exactly one. Regions
/// are numbered 0 ... K-1 in row-major order of their first cell; geo_region receives the
/// number of each cell. Errors: invalid_argument for an invalid grid, a span without one value
/// per cell or an environment value out of range; out_of_memory.
[[nodiscard]] core::Expected<GeoRegions> label_geo_regions(
    std::span<const std::uint8_t> environment, std::span<const float> volcanism,
    const core::Grid& grid, std::span<std::int32_t> geo_region,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::geology
