#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "support/fixtures.hpp"
#include "wg/core/version.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/params/hash.hpp"

namespace wg::test {

// A small world for the io tests (A09): fields of every dtype, every vector layer with awkward
// floats (0.1, -0.0, a denormal), and a provenance with a rejection, consistent with params.

constexpr std::uint64_t kSampleSeed = 0xFEEDFACECAFEBEEFULL;  // above 2^63: tests u64 paths

/// Parameters of the sample world: a 17 x 17 grid.
inline params::GenParams sample_params() {
  params::GenParams p;
  p.grid.size = 17;
  p.grid.dx_m = 2500.0;
  p.grid.hu_m = 12.5;
  return p;
}

inline core::VectorLayers sample_layers() {
  core::VectorLayers L;
  L.peaks = {
      {.cell = 5, .height_eu = 0.1F, .radius_cells = 3.5F},
      {.cell = 288, .height_eu = -0.0F, .radius_cells = std::numeric_limits<float>::denorm_min()}};
  L.volcanoes = {{.cell = 7, .magma_connected = true}, {.cell = 9, .magma_connected = false}};
  L.river_graph.cell = {10, 11, 12};
  L.river_graph.downstream = {1, 2, -1};
  L.river_graph.discharge_m3_yr = {1.0e9F, 2.5e9F, 3.3333333e9F};
  L.river_graph.width_class = {0, 1, 5};
  L.river_graph.order = {1, 1, 2};
  L.river_graph.name_id = {-1, 4, 4};
  L.lakes = {{.id = 1,
              .cells = {20, 21, 37},
              .spill_elevation_eu = 101.25F,
              .outlet_cell = 38,
              .salinity_class = 2}};
  L.regions = {{.id = 3,
                .type = 2,
                .area_cells = 120,
                .centroid_x = 4.125,
                .centroid_y = 1.0 / 3.0,
                .biomes = {1, 5, 9},
                .size_class = 1,
                .name_id = -1}};
  L.plates = {{.id = 0,
               .type = core::PlateType::oceanic,
               .centroid_x = 8.0,
               .centroid_y = 8.5,
               .translation_x = -0.25,
               .translation_y = 1e-17,
               .omega = 0.003,
               .boundaries = {{.other_plate = 1,
                               .boundary_class = core::BoundaryClass::divergent,
                               .cells = {40, 41},
                               .convergence = -0.5,
                               .shear = 0.125}}}};
  L.geo_columns = {
      {.geo_region = 2,
       .layers = {{.rock_id = 4, .thickness_m = 120.5F}, {.rock_id = 7, .thickness_m = 3000.0F}},
       .soil_layers = {{.soil_id = 1, .thickness_m = 0.75F}},
       .caverns =
           {{.level = 1, .depth_m = 900.25F, .openness = 40, .passage_density = 70, .water = 5},
            {.level = 2, .depth_m = 2100.0F, .openness = 100, .passage_density = 1, .water = 0}},
       .magma_depth_m = 3121.25F}};
  L.minerals = {{.mineral_id = 12,
                 .host_layer = 1,
                 .inclusion = core::InclusionType::cluster_small,
                 .cell = 99}};
  return L;
}

/// Writes values into field id of state.
template <class T, class F>
void fill_field(core::WorldState& state, core::FieldId id, const F& value) {
  auto view = state.write<T>(id);
  REQUIRE(view);
  for (std::size_t i = 0; i < view->data.size(); ++i) {
    view->data[i] = value(i);
  }
}

/// The sample world: latitude and elevation (f32), water (u8), biome (u16), plate_id (i32) and
/// flow_accum (f32 without an upper bound), the sample layers and a provenance.
inline core::WorldState sample_world(const params::GenParams& p = sample_params()) {
  const core::Grid grid{.n = p.grid.size, .dx_m = p.grid.dx_m, .hu_m = p.grid.hu_m};
  auto created = core::WorldState::create(grid);
  REQUIRE(created);
  core::WorldState state = std::move(*created);
  const std::vector<float> noise = white_noise(grid, 3);
  fill_field<float>(state, core::FieldId::latitude,
                    [&](std::size_t i) { return static_cast<float>(i) * 0.001F; });
  fill_field<float>(state, core::FieldId::elevation,
                    [&](std::size_t i) { return (static_cast<float>(i % 17) * 25.0F) + noise[i]; });
  fill_field<std::uint8_t>(state, core::FieldId::water,
                           [](std::size_t i) { return static_cast<std::uint8_t>(i % 4); });
  fill_field<std::uint16_t>(state, core::FieldId::biome,
                            [](std::size_t i) { return static_cast<std::uint16_t>(i * 211); });
  fill_field<std::int32_t>(state, core::FieldId::plate_id,
                           [](std::size_t i) { return static_cast<std::int32_t>(i) - 100; });
  fill_field<float>(state, core::FieldId::flow_accum,
                    [&](std::size_t i) { return 6.25e6F * static_cast<float>(1 + (i % 50)); });
  state.layers() = sample_layers();
  core::Provenance& prov = state.provenance();
  prov.profile = "mock";
  prov.seed = kSampleSeed;
  prov.params_hash = params::params_hash(p);
  prov.library_version = core::version();
  prov.attempt = 1;
  prov.completed_stages = {"m01_seed", "m02_shape"};
  prov.rejections = {{.attempt = 0,
                      .stage = "m02_shape",
                      .check_id = "MOCK_CHECK",
                      .measured = 0.1,
                      .required = 2.0}};
  return state;
}

/// Bytes of field id of state.
template <class T>
std::span<const std::byte> field_bytes(const core::WorldState& state, core::FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return std::as_bytes(view->data);
}

}  // namespace wg::test
