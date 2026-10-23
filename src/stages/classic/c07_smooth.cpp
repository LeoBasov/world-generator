// C7 Final smoothing (S7, A13): the Gaussian bumps of the peaks (R4.3) and volcanoes (R4.5), a
// masked Laplacian smoothing of the land, then the downstream constraint along the river graph
// (plan 7.1), so every river still flows downhill. ADR 0011 records the order and the mask.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/numerics/laplacian.hpp"
#include "wg/terrain/bumps.hpp"
#include "wg/terrain/elevation.hpp"
#include "wg/terrain/river_descent.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

class C07Smooth final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC07Smooth; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, LayerId::river_graph, LayerId::peaks, LayerId::volcanoes};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override { return {FieldId::elevation}; }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c07_smooth: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
    if (!z) {
      return failed(std::move(z.error()));
    }
    const core::Grid& grid = ctx.state.grid();
    const core::VectorLayers& layers = ctx.state.layers();
    const params::ElevationPostParams& e = ctx.params.elevation_post;

    // Bumps in layer order: peaks first, then volcanoes.
    for (const core::Peak& p : layers.peaks) {
      if (core::Status s = terrain::add_gaussian_bump(*z, grid, p.cell, e.peak_height,
                                                      p.radius_cells, terrain::kMaxElevation);
          !s.ok()) {
        return failed(std::move(s));
      }
    }
    for (const core::Volcano& v : layers.volcanoes) {
      if (core::Status s = terrain::add_gaussian_bump(*z, grid, v.cell, e.volcano_height,
                                                      e.volcano_width, terrain::kMaxElevation);
          !s.ok()) {
        return failed(std::move(s));
      }
    }

    // Land smoothing; peak and volcano cells keep their tops, ocean cells stay as they are.
    std::vector<std::uint8_t> mask(z->size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
      mask[i] = (*z)[i] >= terrain::kSeaLevel ? 1 : 0;
    }
    for (const core::Peak& p : layers.peaks) {
      mask[static_cast<std::size_t>(p.cell)] = 0;
    }
    for (const core::Volcano& v : layers.volcanoes) {
      mask[static_cast<std::size_t>(v.cell)] = 0;
    }
    if (ctx.cancel.requested()) {
      return StageResult::cancelled();
    }
    const num::SmoothParams smooth{.alpha = e.final_alpha, .iterations = e.final_iterations};
    if (core::Status s = num::laplacian_smooth(*z, mask, grid, smooth, ctx.exec); !s.ok()) {
      return failed(std::move(s));
    }

    if (core::Status s = terrain::enforce_river_descent(
            *z, grid, layers.river_graph, ctx.params.rivers.min_drop, terrain::kMaxElevation);
        !s.ok()) {
      return failed(std::move(s));
    }
    StageResult result = StageResult::ok();
    result.metrics.add("river_nodes", static_cast<double>(layers.river_graph.size()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c07_smooth() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C07Smooth());
}

}  // namespace wg::stages
