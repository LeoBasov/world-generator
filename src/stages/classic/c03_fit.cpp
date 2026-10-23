// C3 Fit and first check (S3, A13): the hypsometric remap z' = G^-1(F(z)) of R4.1, then the
// checks COMPLETE_OCEAN_EDGE_MIN, PARTIAL_OCEAN_EDGE_MIN and PEAK_NUMBER_MIN (R11). A failed
// edge-ocean check is repaired by lowering the edges (R8); ADR 0011 records the rules.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/terrain/edge_ocean.hpp"
#include "wg/terrain/elevation.hpp"
#include "wg/terrain/hypsometry.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

constexpr std::string_view kCompleteEdges = "COMPLETE_OCEAN_EDGE_MIN";
constexpr std::string_view kPartialEdges = "PARTIAL_OCEAN_EDGE_MIN";
constexpr std::string_view kPeakNumber = "PEAK_NUMBER_MIN";

// Drops the peaks whose cell is no longer at the top of the scale, then runs the checks.
StageResult check(StageContext& ctx, std::span<const float> z) {
  std::vector<core::Peak>& peaks = ctx.state.layers().peaks;
  const core::Grid& grid = ctx.state.grid();
  std::erase_if(peaks, [&](const core::Peak& p) {
    return !grid.contains(p.cell) || z[static_cast<std::size_t>(p.cell)] < terrain::kMaxElevation;
  });
  core::Expected<terrain::EdgeOceans> edges = terrain::count_ocean_edges(z, grid);
  if (!edges) {
    return failed(std::move(edges.error()));
  }
  const auto ocean = static_cast<double>(
      std::count_if(z.begin(), z.end(), [](float v) { return v < terrain::kSeaLevel; }));

  const params::ChecksParams& c = ctx.params.checks;
  StageResult result = StageResult::ok();
  if (edges->complete < c.complete_ocean_edge_min) {
    result = StageResult::reject(kCompleteEdges, edges->complete, c.complete_ocean_edge_min);
  } else if (edges->partial < c.partial_ocean_edge_min) {
    result = StageResult::reject(kPartialEdges, edges->partial, c.partial_ocean_edge_min);
  } else if (std::cmp_less(peaks.size(), c.peak_number_min)) {
    result = StageResult::reject(kPeakNumber, static_cast<double>(peaks.size()), c.peak_number_min);
  }
  result.metrics.add("ocean_fraction", ocean / static_cast<double>(z.size()));
  result.metrics.add("ocean_edges_partial", edges->partial);
  result.metrics.add("ocean_edges_complete", edges->complete);
  result.metrics.add("peaks", static_cast<double>(peaks.size()));
  return result;
}

class C03Fit final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC03Fit; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, LayerId::peaks};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override {
    return {FieldId::elevation, LayerId::peaks};
  }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
      if (!z) {
        return failed(std::move(z.error()));
      }
      const params::ElevationPostParams& e = ctx.params.elevation_post;
      if (e.hypsometry_enabled) {
        const terrain::Hypsometry g{.z = e.hypsometry_z, .cdf = e.hypsometry_cdf};
        if (core::Status s = terrain::hypsometric_remap(*z, g, ctx.exec, &ctx.cancel); !s.ok()) {
          return failed(std::move(s));
        }
      }
      return check(ctx, *z);
    } catch (...) {
      return out_of_memory();
    }
  }

  StageResult repair(StageContext& ctx, const StageResult& rejected) noexcept override {
    if (rejected.check_id != kCompleteEdges && rejected.check_id != kPartialEdges) {
      return rejected;
    }
    try {
      core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
      if (!z) {
        return failed(std::move(z.error()));
      }
      const params::ChecksParams& c = ctx.params.checks;
      if (core::Status s = terrain::enforce_edge_oceans(
              *z, ctx.state.grid(), c.complete_ocean_edge_min, c.partial_ocean_edge_min,
              ctx.params.elevation_post.edge_ocean_width);
          !s.ok()) {
        return failed(std::move(s));
      }
      return check(ctx, *z);
    } catch (...) {
      return out_of_memory();
    }
  }

 private:
  static StageResult out_of_memory() noexcept {
    return StageResult::error(
        core::make_status(core::StatusCode::out_of_memory, "c03_fit: out of memory"));
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c03_fit() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C03Fit());
}

}  // namespace wg::stages
