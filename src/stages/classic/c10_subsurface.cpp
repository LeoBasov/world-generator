// C10 Subsurface (S10, A21): geological regions, stone columns with soils, caverns and the magma
// sea (R10.1, R10.4), soil depth, aquifers (R10.3) and mineral deposits (R10.2). ADR 0019
// records the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/core/assert.hpp"
#include "wg/geology/aquifer.hpp"
#include "wg/geology/column.hpp"
#include "wg/geology/minerals.hpp"
#include "wg/geology/regions.hpp"
#include "wg/geology/soil.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

/// Biome of the sand deserts in the biome table.
constexpr std::string_view kSandDesertBiome = "DESERT_SAND";

class C10Subsurface final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC10Subsurface; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::volcanism, FieldId::drainage, FieldId::elevation, FieldId::biome,
            LayerId::volcanoes};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::soil_depth, FieldId::geo_region, FieldId::aquifer, LayerId::geo_columns,
            LayerId::minerals};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c10_subsurface: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<const params::DataTables*> tables = data_tables(ctx, kC10Subsurface);
    if (!tables) {
      return failed(std::move(tables.error()));
    }
    core::Expected<std::span<const float>> z = read_field<float>(ctx, FieldId::elevation);
    core::Expected<std::span<const float>> v = read_field<float>(ctx, FieldId::volcanism);
    core::Expected<std::span<const float>> d = read_field<float>(ctx, FieldId::drainage);
    for (auto* field : {&z, &v, &d}) {
      if (!*field) {
        return failed(std::move(field->error()));
      }
    }
    core::Expected<std::span<const params::BiomeId>> biome =
        read_field<params::BiomeId>(ctx, FieldId::biome);
    if (!biome) {
      return failed(std::move(biome.error()));
    }
    const params::DataTables& t = **tables;
    core::Expected<geology::GeologyTables> geo = geology::GeologyTables::create(t.rocks, t.soils);
    if (!geo) {
      return failed(std::move(geo.error()));
    }
    core::Expected<geology::MineralHosts> hosts =
        geology::MineralHosts::create(t.minerals, t.rocks);
    if (!hosts) {
      return failed(std::move(hosts.error()));
    }
    const core::Grid& grid = ctx.state.grid();
    const params::GeologyParams& gp = ctx.params.geology;
    const auto cells = static_cast<std::size_t>(grid.cells());

    // Environment and regions.
    geology::EnvironmentParams env_params{.deep_ocean_elevation = gp.deep_ocean_elevation,
                                          .mountain_biome = t.biomes.mountain,
                                          .has_sand_desert = false,
                                          .sand_desert_biome = 0};
    if (const params::Biome* sand = t.biomes.find(kSandDesertBiome); sand != nullptr) {
      env_params.has_sand_desert = true;
      env_params.sand_desert_biome = sand->id;
    }
    std::vector<std::uint8_t> environment(cells);
    if (core::Status s = geology::classify_environment(*z, *biome, grid, env_params, environment,
                                                       ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    core::Expected<std::span<std::int32_t>> geo_region =
        write_field<std::int32_t>(ctx, FieldId::geo_region);
    if (!geo_region) {
      return failed(std::move(geo_region.error()));
    }
    core::Expected<geology::GeoRegions> regions =
        geology::label_geo_regions(environment, *v, grid, *geo_region);
    if (!regions) {
      return failed(std::move(regions.error()));
    }

    // Soil depth.
    core::Expected<std::span<float>> soil = write_field<float>(ctx, FieldId::soil_depth);
    if (!soil) {
      return failed(std::move(soil.error()));
    }
    if (core::Status s =
            geology::compute_soil_depth(*z, environment, grid, gp, *soil, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }

    // Columns.
    std::vector<std::uint8_t> volcano(regions->size(), 0);
    for (const core::Volcano& vo : ctx.state.layers().volcanoes) {
      if (grid.contains(vo.cell)) {
        volcano[static_cast<std::size_t>((*geo_region)[static_cast<std::size_t>(vo.cell)])] = 1;
      }
    }
    std::vector<core::GeoColumn> columns;
    columns.reserve(regions->size());
    std::vector<std::uint8_t> has_aquifer(regions->size(), 0);
    for (std::size_t r = 0; r < regions->size(); ++r) {
      if (ctx.cancel.requested()) {
        return StageResult::cancelled();
      }
      const geology::GeoRegion& g = regions->regions[r];
      double soil_sum = 0.0;
      for (const std::int64_t c : regions->cells_of(r)) {
        soil_sum += static_cast<double>((*soil)[static_cast<std::size_t>(c)]);
      }
      core::Expected<core::GeoColumn> column =
          geology::draw_column(*geo, gp,
                               {.region = g.id,
                                .environment = g.environment,
                                .volcanism = g.mean_volcanism,
                                .volcano = volcano[r] != 0,
                                .soil_depth_m = soil_sum / static_cast<double>(g.area_cells),
                                .hu_m = grid.hu_m},
                               ctx.key);
      if (!column) {
        return failed(std::move(column.error()));
      }
      WG_ASSERT(geology::check_column(*column, *geo).ok(), "c10_subsurface: invalid column");
      has_aquifer[r] = geology::column_has_aquifer(*column, *geo) ? 1 : 0;
      columns.push_back(std::move(*column));
    }

    // Aquifers.
    core::Expected<std::span<std::uint8_t>> aquifer =
        write_field<std::uint8_t>(ctx, FieldId::aquifer);
    if (!aquifer) {
      return failed(std::move(aquifer.error()));
    }
    if (core::Status s = geology::assign_aquifers({.elevation = *z,
                                                   .drainage = *d,
                                                   .environment = environment,
                                                   .geo_region = *geo_region,
                                                   .region_has_aquifer = has_aquifer},
                                                  grid, gp.df_heavy_aquifer_quirk, ctx.key,
                                                  *aquifer, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }

    // Minerals.
    core::Expected<std::vector<core::MineralDeposit>> deposits =
        geology::place_minerals(*hosts, columns, *regions, grid.hu_m, gp.mineral_scarcity, ctx.key);
    if (!deposits) {
      return failed(std::move(deposits.error()));
    }

    StageResult result = StageResult::ok();
    result.metrics.add("geo_regions", static_cast<double>(columns.size()));
    result.metrics.add("deposits", static_cast<double>(deposits->size()));
    ctx.state.layers().geo_columns = std::move(columns);
    ctx.state.layers().minerals = std::move(*deposits);
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c10_subsurface() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C10Subsurface());
}

}  // namespace wg::stages
