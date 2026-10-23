// C2 Seed primary fields (S2, A12): the six fractal fields of R3, the latitude bias of
// temperature (R5.3) and the peak candidates of R4.3. Each field has its own key,
// key.derive(<field name>); ADR 0010 records the layout.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "numerics/common.hpp"
#include "stages/classic/classic_stages.hpp"
#include "wg/climate/latitude.hpp"
#include "wg/fractal/fractal.hpp"
#include "wg/fractal/peaks.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

// The fields that need no post-processing in this stage, with their parameters.
struct PlainField {
  FieldId id;
  const params::FieldGenParams params::FieldsParams::*params;
};

constexpr std::array<PlainField, 4> kPlainFields = {{
    {FieldId::rainfall, &params::FieldsParams::rainfall},
    {FieldId::drainage, &params::FieldsParams::drainage},
    {FieldId::volcanism, &params::FieldsParams::volcanism},
    {FieldId::savagery, &params::FieldsParams::savagery},
}};

core::RngKey field_key(const StageContext& ctx, FieldId id) noexcept {
  return ctx.key.derive(core::field_info(id).name);
}

core::Expected<std::span<float>> field(StageContext& ctx, FieldId id) noexcept {
  core::Expected<core::FieldView<float>> view = ctx.state.write<float>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return view->data;
}

// Elevation without clamping, the peak candidates on those raw values, then the clamp.
core::Status seed_elevation(StageContext& ctx) noexcept {
  core::Expected<std::span<float>> z = field(ctx, FieldId::elevation);
  if (!z) {
    return std::move(z.error());
  }
  const params::FieldGenParams& p = ctx.params.fields.elevation;
  core::Status status = fractal::generate_unclamped_field(
      ctx.state.grid(), p, field_key(ctx, FieldId::elevation), {}, *z, ctx.exec, &ctx.cancel);
  if (!status.ok()) {
    return status;
  }
  // A peak needs z = 400 after the clamp, so a lower maximum allows none (R4.3).
  std::vector<core::Peak>& peaks = ctx.state.layers().peaks;
  if (p.max >= static_cast<double>(fractal::kPeakElevation)) {
    status = fractal::find_peak_candidates(*z, ctx.state.grid(), fractal::kPeakElevation,
                                           static_cast<float>(ctx.params.elevation_post.peak_width),
                                           peaks);
    if (!status.ok()) {
      return status;
    }
  } else {
    peaks.clear();
  }
  return fractal::clamp_field(*z, p.min, p.max, ctx.exec);
}

// T = T_0 + T_phi(phi) (R5.3): the fractal base field, clamped to [min, max], plus the
// latitude term. The sum is kept inside the registry range of temperature.
core::Status seed_temperature(StageContext& ctx) noexcept {
  core::Expected<core::FieldView<const float>> phi = ctx.state.read<FieldId::latitude>();
  if (!phi) {
    return std::move(phi.error());
  }
  core::Expected<std::span<float>> t = field(ctx, FieldId::temperature);
  if (!t) {
    return std::move(t.error());
  }
  const core::Grid& grid = ctx.state.grid();
  core::Status status =
      fractal::generate_field(grid, ctx.params.fields.temperature,
                              field_key(ctx, FieldId::temperature), {}, *t, ctx.exec, &ctx.cancel);
  if (!status.ok()) {
    return status;
  }
  const double delta_t = ctx.params.temperature.equator_pole_delta;
  const core::FieldRange& range = core::field_info(FieldId::temperature).range;
  const std::span<const float> lat = phi->data;
  const std::span<float> out = *t;
  const std::int64_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      const double v = static_cast<double>(out[k]) +
                       climate::latitude_term(static_cast<double>(lat[k]), delta_t);
      out[k] = static_cast<float>(std::clamp(v, range.min, range.max));
    }
  };
  return core::parallel_for(ctx.exec, 0, n, num::detail::kRowGrain, rows, &ctx.cancel);
}

class C02Seed final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC02Seed; }
  [[nodiscard]] FieldSet inputs() const noexcept override { return {FieldId::latitude}; }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::elevation, FieldId::rainfall, FieldId::temperature, FieldId::drainage,
            FieldId::volcanism, FieldId::savagery, LayerId::peaks};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c02_seed: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Status status = seed_elevation(ctx);
    if (status.ok()) {
      status = seed_temperature(ctx);
    }
    for (const PlainField& f : kPlainFields) {
      if (!status.ok()) {
        break;
      }
      core::Expected<std::span<float>> out = field(ctx, f.id);
      if (!out) {
        return failed(std::move(out.error()));
      }
      status = fractal::generate_field(ctx.state.grid(), ctx.params.fields.*f.params,
                                       field_key(ctx, f.id), {}, *out, ctx.exec, &ctx.cancel);
    }
    if (!status.ok()) {
      return failed(std::move(status));
    }
    StageResult result = StageResult::ok();
    result.metrics.add("peak_candidates", static_cast<double>(ctx.state.layers().peaks.size()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c02_seed() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C02Seed());
}

}  // namespace wg::stages
