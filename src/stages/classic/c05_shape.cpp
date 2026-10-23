// C5 Terrain shaping (S5, A13): plains by mid-level smoothing (R4.4), then one volcano per area
// of volcanism 100 and the check VOLCANO_MIN (R4.5). ADR 0011 records the rules.

#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/terrain/plains.hpp"
#include "wg/terrain/volcanoes.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

class C05Shape final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC05Shape; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, FieldId::volcanism};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::elevation, LayerId::volcanoes};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c05_shape: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
    if (!z) {
      return failed(std::move(z.error()));
    }
    const params::ElevationPostParams& e = ctx.params.elevation_post;
    const terrain::PlainsParams plains{.z_lo = e.plains_z_lo,
                                       .z_hi = e.plains_z_hi,
                                       .taper = e.plains_taper,
                                       .lambda = e.plains_lambda,
                                       .iterations = e.plains_iterations};
    const core::Grid& grid = ctx.state.grid();
    if (core::Status s = terrain::plains_smooth(*z, grid, plains, ctx.exec, &ctx.cancel); !s.ok()) {
      return failed(std::move(s));
    }

    core::Expected<core::FieldView<const float>> v = ctx.state.read<FieldId::volcanism>();
    if (!v) {
      return failed(std::move(v.error()));
    }
    std::vector<core::Volcano>& volcanoes = ctx.state.layers().volcanoes;
    if (core::Status s =
            terrain::place_volcanoes(v->data, grid, terrain::kVolcanoVolcanism, volcanoes);
        !s.ok()) {
      return failed(std::move(s));
    }

    const std::int32_t required = ctx.params.checks.volcano_min;
    StageResult result =
        std::cmp_less(volcanoes.size(), required)
            ? StageResult::reject("VOLCANO_MIN", static_cast<double>(volcanoes.size()), required)
            : StageResult::ok();
    result.metrics.add("volcanoes", static_cast<double>(volcanoes.size()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c05_shape() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C05Shape());
}

}  // namespace wg::stages
