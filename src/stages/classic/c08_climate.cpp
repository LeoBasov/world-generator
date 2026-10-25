// C8 Climate recomputation (S8, A20): orographic rainfall on the final terrain (R7.2), the
// temperature terms that need it (R5.3), vegetation (R8.1), salinity and the lake classes
// (R8.2). ADR 0012, 0016, 0017 and 0018 record the order and the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

#include "stages/classic/classic_stages.hpp"
#include "wg/bio/salinity.hpp"
#include "wg/bio/vegetation.hpp"
#include "wg/climate/precipitation.hpp"
#include "wg/climate/temperature.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

class C08Climate final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC08Climate; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, FieldId::rainfall, FieldId::temperature, FieldId::vegetation,
            FieldId::drainage,  FieldId::water,    LayerId::river_graph, LayerId::lakes};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::rainfall, FieldId::temperature, FieldId::vegetation, FieldId::salinity,
            LayerId::lakes};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c08_climate: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<const params::DataTables*> tables = data_tables(ctx, kC08Climate);
    if (!tables) {
      return failed(std::move(tables.error()));
    }
    core::Expected<std::span<const float>> z = read_field<float>(ctx, FieldId::elevation);
    if (!z) {
      return failed(std::move(z.error()));
    }
    core::Expected<std::span<const float>> d = read_field<float>(ctx, FieldId::drainage);
    if (!d) {
      return failed(std::move(d.error()));
    }
    core::Expected<std::span<const std::uint8_t>> water =
        read_field<std::uint8_t>(ctx, FieldId::water);
    if (!water) {
      return failed(std::move(water.error()));
    }
    core::Expected<std::span<float>> p = update_float(ctx, FieldId::rainfall);
    if (!p) {
      return failed(std::move(p.error()));
    }
    core::Expected<std::span<float>> t = update_float(ctx, FieldId::temperature);
    if (!t) {
      return failed(std::move(t.error()));
    }
    core::Expected<std::span<float>> v = update_float(ctx, FieldId::vegetation);
    if (!v) {
      return failed(std::move(v.error()));
    }
    const core::Grid& grid = ctx.state.grid();

    // 1. Rainfall on the final terrain, in place.
    if (core::Status s = climate::orographic_precipitation(
            grid, {.base = *p, .elevation = *z, .drainage = *d}, ctx.params.precipitation, *p, {},
            ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    // 2. Temperature in place: C2's T_0 + T_phi plus the lapse term, the forest term of C4's
    // vegetation and the rain term of the new rainfall (no second latitude term).
    if (core::Status s = climate::compute_temperature(
            grid, {.base = *t, .latitude = {}, .elevation = *z, .vegetation = *v, .rainfall = *p},
            ctx.params.temperature, *t, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    // 3. Vegetation from the new climate, with the lakes of C6.
    if (core::Status s = bio::compute_vegetation(
            grid,
            {.temperature = *t, .rainfall = *p, .elevation = *z, .drainage = *d, .water = *water},
            ctx.params.vegetation, ctx.params.temperature, *v, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    // 4. Salinity with the estuaries, 5. lake classes, written back to the lake cells.
    core::Expected<std::span<std::uint8_t>> salinity =
        write_field<std::uint8_t>(ctx, FieldId::salinity);
    if (!salinity) {
      return failed(std::move(salinity.error()));
    }
    core::VectorLayers& layers = ctx.state.layers();
    if (core::Status s = bio::compute_salinity(
            grid, {.water = *water, .rivers = &layers.river_graph}, ctx.params.salinity,
            (*tables)->salinity, *salinity, ctx.exec, &ctx.cancel);
        !s.ok()) {
      return failed(std::move(s));
    }
    if (core::Status s = bio::assign_lake_salinity(*salinity, grid, layers.lakes); !s.ok()) {
      return failed(std::move(s));
    }
    for (const core::Lake& lake : layers.lakes) {
      for (const std::int64_t cell : lake.cells) {
        (*salinity)[static_cast<std::size_t>(cell)] = lake.salinity_class;
      }
    }
    return StageResult::ok();
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c08_climate() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C08Climate());
}

}  // namespace wg::stages
