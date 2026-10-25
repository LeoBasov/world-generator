#include "wg/geology/regions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "numerics/common.hpp"
#include "wg/numerics/labeling.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::geology {

namespace {

namespace detail = num::detail;

// Neighbours that precede a cell in row-major order, i.e. visited before it.
constexpr std::array<core::Dir8, 4> kEarlier = {core::Dir8::w, core::Dir8::nw, core::Dir8::n,
                                                core::Dir8::ne};

}  // namespace

std::uint8_t volcanism_band(float volcanism) noexcept {
  if (!(volcanism >= 0.0F)) {
    return 0;
  }
  const double band = std::floor(static_cast<double>(volcanism) / kVolcanismBandWidth);
  return static_cast<std::uint8_t>(std::min(band, static_cast<double>(kVolcanismBands - 1)));
}

core::Status classify_environment(std::span<const float> elevation,
                                  std::span<const params::BiomeId> biome, const core::Grid& grid,
                                  const EnvironmentParams& params,
                                  std::span<std::uint8_t> environment, core::Executor& exec,
                                  const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(elevation, grid,
                           "classify_environment: elevation must have one value per cell"),
       detail::check_field(biome, grid, "classify_environment: biome must have one value per cell"),
       detail::check_field(environment, grid,
                           "classify_environment: environment must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const auto deep = static_cast<float>(params.deep_ocean_elevation);
  return core::parallel_for(
      exec, 0, grid.cells(), detail::kRowGrain * grid.n,
      [&](const core::ChunkRange& r) noexcept {
        for (std::int64_t i = r.begin; i < r.end; ++i) {
          const auto c = static_cast<std::size_t>(i);
          GeoEnvironment e = GeoEnvironment::land;
          if (elevation[c] < deep) {
            e = GeoEnvironment::deep_ocean;
          } else if (elevation[c] < terrain::kSeaLevel) {
            e = GeoEnvironment::shallow_ocean;
          } else if (biome[c] == params.mountain_biome) {
            e = GeoEnvironment::mountain;
          } else if (params.has_sand_desert && biome[c] == params.sand_desert_biome) {
            e = GeoEnvironment::sand_desert;
          }
          environment[c] = static_cast<std::uint8_t>(e);
        }
      },
      cancel);
}

core::Expected<GeoRegions> label_geo_regions(std::span<const std::uint8_t> environment,
                                             std::span<const float> volcanism,
                                             const core::Grid& grid,
                                             std::span<std::int32_t> geo_region,
                                             core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(environment, grid,
                           "label_geo_regions: environment must have one value per cell"),
       detail::check_field(volcanism, grid,
                           "label_geo_regions: volcanism must have one value per cell"),
       detail::check_field(geo_region, grid,
                           "label_geo_regions: geo_region must have one value per cell")});
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  if (std::any_of(environment.begin(), environment.end(),
                  [](std::uint8_t e) { return e >= kGeoEnvironmentCount; })) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "label_geo_regions: environment value out of range");
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<num::UnionFind> sets = num::UnionFind::create(cells, resource);
  if (!sets) {
    return core::make_error(std::move(sets.error()));
  }
  try {
    // The class of a cell: environment and volcanism band.
    std::vector<std::uint8_t> cls(cells);
    for (std::size_t i = 0; i < cells; ++i) {
      cls[i] = static_cast<std::uint8_t>((environment[i] * kVolcanismBands) +
                                         volcanism_band(volcanism[i]));
    }
    // Pass 1: join every cell with its earlier neighbours of the same class.
    for (std::size_t i = 0; i < cells; ++i) {
      for (const core::Dir8 d : kEarlier) {
        const std::int64_t j = grid.neighbor(static_cast<std::int64_t>(i), d);
        if (j != core::kNoCell && cls[static_cast<std::size_t>(j)] == cls[i]) {
          sets->unite(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
        }
      }
    }
    // Pass 2: a root is its region's first cell in row-major order, so regions are numbered
    // when their root is reached.
    GeoRegions out;
    std::vector<double> sum_v;
    for (std::size_t i = 0; i < cells; ++i) {
      const auto root = static_cast<std::size_t>(sets->find(static_cast<std::int32_t>(i)));
      if (root == i) {
        geo_region[i] = static_cast<std::int32_t>(out.regions.size());
        GeoRegion& g = out.regions.emplace_back();
        g.id = geo_region[i];
        g.environment = static_cast<GeoEnvironment>(environment[i]);
        g.volcanism_band = volcanism_band(volcanism[i]);
        sum_v.push_back(0.0);
      } else {
        geo_region[i] = geo_region[root];
      }
      const auto id = static_cast<std::size_t>(geo_region[i]);
      ++out.regions[id].area_cells;
      const float v = volcanism[i];
      sum_v[id] += std::isfinite(v) ? static_cast<double>(v) : 0.0;
    }
    // Counting sort of the cells by region keeps them ascending within a region.
    const std::size_t k = out.regions.size();
    out.offsets.assign(k + 1, 0);
    for (std::size_t id = 0; id < k; ++id) {
      GeoRegion& g = out.regions[id];
      g.mean_volcanism = sum_v[id] / static_cast<double>(g.area_cells);
      out.offsets[id + 1] = out.offsets[id] + g.area_cells;
    }
    out.cells.resize(cells);
    std::vector<std::int64_t> next(out.offsets.begin(), out.offsets.end() - 1);
    for (std::size_t i = 0; i < cells; ++i) {
      const auto id = static_cast<std::size_t>(geo_region[i]);
      out.cells[static_cast<std::size_t>(next[id]++)] = static_cast<std::int64_t>(i);
    }
    return out;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "label_geo_regions: no memory");
  }
}

}  // namespace wg::geology
