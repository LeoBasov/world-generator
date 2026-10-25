// C4 First vegetation (S4, A20): vegetation from the first climate (R8.1), the provisional biome
// without water and salinity (R9.1-R9.2) and the initial REGION_COUNTS (R11). ADR 0017 and 0018
// record the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/bio/vegetation.hpp"
#include "wg/classify/biome.hpp"
#include "wg/classify/checks.hpp"
#include "wg/classify/regions.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::StageContext;
using pipeline::StageResult;

class C04Vegetation final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC04Vegetation; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, FieldId::rainfall, FieldId::temperature, FieldId::drainage};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::vegetation, FieldId::biome};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c04_vegetation: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<const params::DataTables*> tables = data_tables(ctx, kC04Vegetation);
    if (!tables) {
      return failed(std::move(tables.error()));
    }
    core::Expected<std::span<const float>> z = read_field<float>(ctx, FieldId::elevation);
    core::Expected<std::span<const float>> p = read_field<float>(ctx, FieldId::rainfall);
    core::Expected<std::span<const float>> t = read_field<float>(ctx, FieldId::temperature);
    core::Expected<std::span<const float>> d = read_field<float>(ctx, FieldId::drainage);
    for (auto* field : {&z, &p, &t, &d}) {
      if (!*field) {
        return failed(std::move(field->error()));
      }
    }
    const core::Grid& grid = ctx.state.grid();

    // 1. Vegetation, without water (lakes are not known yet).
    core::Expected<std::span<float>> vegetation = write_float(ctx, FieldId::vegetation);
    if (!vegetation) {
      return failed(std::move(vegetation.error()));
    }
    if (core::Status s = bio::compute_vegetation(
            grid, {.temperature = *t, .rainfall = *p, .elevation = *z, .drainage = *d, .water = {}},
            ctx.params.vegetation, ctx.params.temperature, *vegetation, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }

    // 2. Provisional biome: fresh everywhere, ocean from the elevation.
    const params::BiomeTable& table = (*tables)->biomes;
    core::Expected<classify::BiomeClassifier> classifier = classify::BiomeClassifier::create(table);
    if (!classifier) {
      return failed(std::move(classifier.error()));
    }
    core::Expected<std::span<params::BiomeId>> biome =
        write_field<params::BiomeId>(ctx, FieldId::biome);
    if (!biome) {
      return failed(std::move(biome.error()));
    }
    const auto cells = static_cast<std::size_t>(grid.cells());
    std::vector<std::uint8_t> types(cells);
    if (core::Status s =
            classify::classify_biomes(grid,
                                      {.elevation = *z,
                                       .rainfall = *p,
                                       .drainage = *d,
                                       .temperature = *t,
                                       .salinity = {},
                                       .water = {}},
                                      *classifier, *biome, types, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }

    // 3. Initial regions and REGION_COUNTS [0], [1].
    std::vector<std::int32_t> ids(cells);
    core::Expected<std::vector<core::Region>> regions =
        classify::label_regions(types, *biome, grid, table.region_sizes, ids);
    if (!regions) {
      return failed(std::move(regions.error()));
    }
    core::Expected<std::vector<classify::RegionTypeCount>> counts =
        classify::count_region_types(types, *regions, classifier->region_type_count());
    if (!counts) {
      return failed(std::move(counts.error()));
    }
    const std::optional<classify::RegionCheckFailure> failure =
        classify::check_region_counts(*counts, table.region_types, ctx.params.checks.region_counts,
                                      classify::RegionCountPhase::initial);
    StageResult result =
        failure ? StageResult::reject(failure->check_id, failure->measured, failure->required)
                : StageResult::ok();
    result.metrics.add("initial_regions", static_cast<double>(regions->size()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c04_vegetation() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C04Vegetation());
}

}  // namespace wg::stages
