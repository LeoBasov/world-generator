// C6 Erosion and rivers (S6, A15-A17): small oceans, test-river erosion with RIVER_MINS,
// filling, D8 routing, flow accumulation and discharge, lakes, lake bulges and the river
// network. ADR 0013, 0014 and 0015 record the order and the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/hydro/discharge.hpp"
#include "wg/hydro/erosion.hpp"
#include "wg/hydro/lake_bulge.hpp"
#include "wg/hydro/lakes.hpp"
#include "wg/hydro/priority_flood.hpp"
#include "wg/hydro/river_network.hpp"
#include "wg/hydro/routing.hpp"
#include "wg/hydro/small_ocean.hpp"
#include "wg/hydro/test_rivers.hpp"
#include "wg/hydro/water.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

constexpr std::string_view kRiverMinsBefore = "RIVER_MINS[0]";
constexpr std::string_view kRiverMinsAfter = "RIVER_MINS[1]";

class C06Rivers final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC06Rivers; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, FieldId::rainfall, FieldId::drainage};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::elevation, FieldId::flow_receiver, FieldId::flow_accum,
            FieldId::discharge, FieldId::water,         FieldId::river_class,
            FieldId::lake_id,   LayerId::river_graph,   LayerId::lakes};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c06_rivers: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
    if (!z) {
      return failed(std::move(z.error()));
    }
    core::Expected<std::span<const float>> rainfall = read_field<float>(ctx, FieldId::rainfall);
    if (!rainfall) {
      return failed(std::move(rainfall.error()));
    }
    core::Expected<std::span<const float>> drainage = read_field<float>(ctx, FieldId::drainage);
    if (!drainage) {
      return failed(std::move(drainage.error()));
    }
    const core::Grid& grid = ctx.state.grid();
    const params::RiversParams& rp = ctx.params.rivers;

    // 1. Small oceans, 2. test rivers and thermal erosion (A16) with RIVER_MINS.
    core::Expected<std::int32_t> removed =
        hydro::remove_small_oceans(*z, grid, rp.small_ocean_min_area);
    if (!removed) {
      return failed(std::move(removed.error()));
    }
    core::Expected<hydro::ErosionReport> erosion =
        hydro::erode(*z, grid, ctx.params.erosion, ctx.key, {}, &ctx.cancel);
    if (!erosion) {
      return failed(std::move(erosion.error()));
    }
    const params::ChecksParams& checks = ctx.params.checks;
    if (erosion->sources_before < checks.river_mins[0]) {
      return StageResult::reject(kRiverMinsBefore, erosion->sources_before, checks.river_mins[0]);
    }
    if (erosion->sources_after < checks.river_mins[1]) {
      return StageResult::reject(kRiverMinsAfter, erosion->sources_after, checks.river_mins[1]);
    }
    if (ctx.cancel.requested()) {
      return StageResult::cancelled();
    }

    // 3. Fill, 4. route and accumulate.
    const auto cells = static_cast<std::size_t>(grid.cells());
    std::vector<float> level(cells);
    std::vector<float> filled(cells);
    if (core::Status s = hydro::fill_depressions(*z, grid, level); !s.ok()) {
      return failed(std::move(s));
    }
    if (core::Status s = hydro::priority_flood(*z, grid, filled); !s.ok()) {
      return failed(std::move(s));
    }
    core::Expected<std::span<std::int32_t>> receivers =
        write_field<std::int32_t>(ctx, FieldId::flow_receiver);
    if (!receivers) {
      return failed(std::move(receivers.error()));
    }
    if (core::Status s = hydro::d8_receivers(filled, grid, *receivers); !s.ok()) {
      return failed(std::move(s));
    }
    std::vector<std::int32_t> order(cells);
    if (core::Status s = hydro::stack_order(*receivers, grid, order); !s.ok()) {
      return failed(std::move(s));
    }
    std::vector<double> weights(cells);
    std::vector<double> accumulation(cells);
    core::Expected<std::span<float>> area = write_float(ctx, FieldId::flow_accum);
    if (!area) {
      return failed(std::move(area.error()));
    }
    if (core::Status s = hydro::area_weights(*z, grid, weights); !s.ok()) {
      return failed(std::move(s));
    }
    if (core::Status s = hydro::accumulate_flow(*receivers, order, weights, grid, accumulation);
        !s.ok()) {
      return failed(std::move(s));
    }
    for (std::size_t i = 0; i < cells; ++i) {
      (*area)[i] = static_cast<float>(accumulation[i]);
    }
    if (core::Status s = hydro::runoff_weights(*z, *rainfall, *drainage, grid, weights); !s.ok()) {
      return failed(std::move(s));
    }
    if (core::Status s = hydro::accumulate_flow(*receivers, order, weights, grid, accumulation);
        !s.ok()) {
      return failed(std::move(s));
    }
    core::Expected<std::span<float>> discharge = write_float(ctx, FieldId::discharge);
    if (!discharge) {
      return failed(std::move(discharge.error()));
    }
    for (std::size_t i = 0; i < cells; ++i) {
      (*discharge)[i] = static_cast<float>(accumulation[i]);
    }

    // 5. Lakes, 6. lake bulges along a first network, 7. the final network.
    core::Expected<std::span<std::int32_t>> lake_id =
        write_field<std::int32_t>(ctx, FieldId::lake_id);
    if (!lake_id) {
      return failed(std::move(lake_id.error()));
    }
    core::Expected<std::vector<core::Lake>> lakes = hydro::label_lakes(
        *z, level, filled, *receivers, grid, {.min_volume = rp.lake_min_volume}, *lake_id);
    if (!lakes) {
      return failed(std::move(lakes.error()));
    }
    const auto filled_lakes = static_cast<double>(lakes->size());
    core::Expected<std::vector<std::int32_t>> sources = hydro::find_river_sources(*z, grid);
    if (!sources) {
      return failed(std::move(sources.error()));
    }
    core::Expected<core::RiverGraph> rivers =
        hydro::trace_rivers(*sources, *z, *receivers, *lake_id, *lakes, grid);
    if (!rivers) {
      return failed(std::move(rivers.error()));
    }
    core::Expected<std::int32_t> bulges =
        hydro::bulge_lakes(*z, *receivers, *rivers, grid,
                           {.count = rp.lake_bulge_count, .height = rp.lake_bulge_height},
                           ctx.key.derive("lake_bulge"), *lake_id, *lakes);
    if (!bulges) {
      return failed(std::move(bulges.error()));
    }
    rivers = hydro::trace_rivers(*sources, *z, *receivers, *lake_id, *lakes, grid);
    if (!rivers) {
      return failed(std::move(rivers.error()));
    }
    core::Expected<std::vector<hydro::RiverStem>> stems =
        hydro::river_attributes(*rivers, accumulation, grid, rp);
    if (!stems) {
      return failed(std::move(stems.error()));
    }
    if (core::Status s = hydro::check_river_graph(*rivers, *z, *receivers, *lake_id, grid);
        !s.ok()) {
      return failed(core::make_status(core::StatusCode::internal, s.message));
    }
    core::Expected<std::span<std::uint8_t>> water = write_field<std::uint8_t>(ctx, FieldId::water);
    if (!water) {
      return failed(std::move(water.error()));
    }
    core::Expected<std::span<std::uint8_t>> river_class =
        write_field<std::uint8_t>(ctx, FieldId::river_class);
    if (!river_class) {
      return failed(std::move(river_class.error()));
    }
    if (core::Status s = hydro::classify_water(*z, *lake_id, *rivers, grid, *water, *river_class);
        !s.ok()) {
      return failed(std::move(s));
    }

    StageResult result = StageResult::ok();
    result.metrics.add("small_oceans_removed", static_cast<double>(*removed));
    result.metrics.add("sources_before", static_cast<double>(erosion->sources_before));
    result.metrics.add("sources_after", static_cast<double>(erosion->sources_after));
    result.metrics.add("walker_steps", static_cast<double>(erosion->walker_steps));
    result.metrics.add("lakes", filled_lakes);
    result.metrics.add("lake_bulges", static_cast<double>(*bulges));
    result.metrics.add("river_nodes", static_cast<double>(rivers->size()));
    result.metrics.add("rivers", static_cast<double>(stems->size()));
    core::VectorLayers& layers = ctx.state.layers();
    layers.river_graph = std::move(*rivers);
    layers.lakes = std::move(*lakes);
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c06_rivers() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C06Rivers());
}

}  // namespace wg::stages
