// C9 Biomes and regions (S9, A20): the final biome with water and salinity (R9.1-R9.2) and the
// named regions (R9.3). ADR 0018 records the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/classify/biome.hpp"
#include "wg/classify/regions.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

class C09Biomes final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC09Biomes; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation,   FieldId::rainfall, FieldId::drainage,
            FieldId::temperature, FieldId::salinity, FieldId::water};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::biome, FieldId::region_id, LayerId::regions};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c09_biomes: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<const params::DataTables*> tables = data_tables(ctx, kC09Biomes);
    if (!tables) {
      return failed(std::move(tables.error()));
    }
    core::Expected<std::span<const float>> z = read_field<float>(ctx, FieldId::elevation);
    core::Expected<std::span<const float>> p = read_field<float>(ctx, FieldId::rainfall);
    core::Expected<std::span<const float>> d = read_field<float>(ctx, FieldId::drainage);
    core::Expected<std::span<const float>> t = read_field<float>(ctx, FieldId::temperature);
    for (auto* field : {&z, &p, &d, &t}) {
      if (!*field) {
        return failed(std::move(field->error()));
      }
    }
    core::Expected<std::span<const std::uint8_t>> salinity =
        read_field<std::uint8_t>(ctx, FieldId::salinity);
    if (!salinity) {
      return failed(std::move(salinity.error()));
    }
    core::Expected<std::span<const std::uint8_t>> water =
        read_field<std::uint8_t>(ctx, FieldId::water);
    if (!water) {
      return failed(std::move(water.error()));
    }
    const params::BiomeTable& table = (*tables)->biomes;
    core::Expected<classify::BiomeClassifier> classifier = classify::BiomeClassifier::create(table);
    if (!classifier) {
      return failed(std::move(classifier.error()));
    }
    const core::Grid& grid = ctx.state.grid();
    core::Expected<std::span<params::BiomeId>> biome =
        write_field<params::BiomeId>(ctx, FieldId::biome);
    if (!biome) {
      return failed(std::move(biome.error()));
    }
    std::vector<std::uint8_t> types(static_cast<std::size_t>(grid.cells()));
    if (core::Status s =
            classify::classify_biomes(grid,
                                      {.elevation = *z,
                                       .rainfall = *p,
                                       .drainage = *d,
                                       .temperature = *t,
                                       .salinity = *salinity,
                                       .water = *water},
                                      *classifier, *biome, types, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    core::Expected<std::span<std::int32_t>> region_id =
        write_field<std::int32_t>(ctx, FieldId::region_id);
    if (!region_id) {
      return failed(std::move(region_id.error()));
    }
    core::Expected<std::vector<core::Region>> regions =
        classify::label_regions(types, *biome, grid, table.region_sizes, *region_id);
    if (!regions) {
      return failed(std::move(regions.error()));
    }
    StageResult result = StageResult::ok();
    result.metrics.add("regions", static_cast<double>(regions->size()));
    ctx.state.layers().regions = std::move(*regions);
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c09_biomes() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C09Biomes());
}

}  // namespace wg::stages
