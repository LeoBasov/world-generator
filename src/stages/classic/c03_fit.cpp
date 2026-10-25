// C3 Fit and first check (S3, A13, A22): the hypsometric remap z' = G^-1(F(z)) of R4.1, then the
// checks COMPLETE_OCEAN_EDGE_MIN, PARTIAL_OCEAN_EDGE_MIN, PEAK_NUMBER_MIN and the five *_RANGES
// (R11). A failed edge-ocean check is repaired by lowering the edges, a failed ELEVATION_RANGES
// by the hypsometry repair (R8); ADR 0011 and ADR 0020 record the rules.

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/classify/field_ranges.hpp"
#include "wg/terrain/band_repair.hpp"
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

// Metric names of the band counts, indexed like classify::kRangeFields.
constexpr std::array<std::array<std::string_view, 3>, 5> kBandMetrics = {{
    {"elevation_low", "elevation_mid", "elevation_high"},
    {"rainfall_low", "rainfall_mid", "rainfall_high"},
    {"drainage_low", "drainage_mid", "drainage_high"},
    {"volcanism_low", "volcanism_mid", "volcanism_high"},
    {"savagery_low", "savagery_mid", "savagery_high"},
}};

bool is_edge_check(std::string_view check_id) {
  return check_id == kCompleteEdges || check_id == kPartialEdges;
}

// Band counts of the five *_RANGES fields; z is the current elevation.
core::Expected<classify::RangeCounts> count_ranges(const StageContext& ctx,
                                                   std::span<const float> z) {
  classify::RangeCounts counts{};
  for (std::size_t f = 0; f < classify::kRangeFields.size(); ++f) {
    const FieldId id = classify::kRangeFields[f];
    std::span<const float> values = z;
    if (id != FieldId::elevation) {
      core::Expected<std::span<const float>> field = read_field<float>(ctx, id);
      if (!field) {
        return core::make_error(std::move(field.error()));
      }
      values = *field;
    }
    core::Expected<classify::RangeBands> bands = classify::range_bands(id);
    if (!bands) {
      return core::make_error(std::move(bands.error()));
    }
    counts[f] = classify::count_bands(values, *bands);
  }
  return counts;
}

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
  core::Expected<classify::RangeCounts> ranges = count_ranges(ctx, z);
  if (!ranges) {
    return failed(std::move(ranges.error()));
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
  } else if (const std::optional<classify::RangeCheckFailure> f =
                 classify::check_field_ranges(*ranges, c)) {
    result = StageResult::reject(f->check_id, f->measured, f->required);
  }
  result.metrics.add("ocean_fraction", ocean / static_cast<double>(z.size()));
  result.metrics.add("ocean_edges_partial", edges->partial);
  result.metrics.add("ocean_edges_complete", edges->complete);
  result.metrics.add("peaks", static_cast<double>(peaks.size()));
  for (std::size_t f = 0; f < kBandMetrics.size(); ++f) {
    for (std::size_t k = 0; k < 3; ++k) {
      result.metrics.add(kBandMetrics[f][k], static_cast<double>((*ranges)[f][k]));
    }
  }
  return result;
}

// The hypsometry repair of ELEVATION_RANGES: true if the minimums hold afterwards.
core::Expected<bool> fit_bands(const StageContext& ctx, std::span<float> z) {
  core::Expected<classify::RangeBands> bands = classify::range_bands(FieldId::elevation);
  if (!bands) {
    return core::make_error(std::move(bands.error()));
  }
  return terrain::fit_elevation_bands(z, {.lo = bands->lo,
                                          .low_max = bands->low_max,
                                          .high_min = bands->high_min,
                                          .hi = bands->hi,
                                          .mins = ctx.params.checks.elevation_ranges});
}

class C03Fit final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC03Fit; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::elevation, FieldId::rainfall, FieldId::drainage,
            FieldId::volcanism, FieldId::savagery, LayerId::peaks};
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

  // Repairs the edge oceans and the elevation bands, each at most once and in the order the
  // checks fail, rechecking after each step; other failures stand.
  StageResult repair(StageContext& ctx, const StageResult& rejected) noexcept override {
    if (!is_edge_check(rejected.check_id) && !classify::is_elevation_ranges(rejected.check_id)) {
      return rejected;
    }
    try {
      core::Expected<std::span<float>> z = update_float(ctx, FieldId::elevation);
      if (!z) {
        return failed(std::move(z.error()));
      }
      const params::ChecksParams& c = ctx.params.checks;
      bool edges_done = false;
      bool bands_done = false;
      StageResult result = rejected;
      while (result.kind == StageResult::Kind::reject) {
        if (is_edge_check(result.check_id) && !edges_done) {
          edges_done = true;
          if (core::Status s = terrain::enforce_edge_oceans(
                  *z, ctx.state.grid(), c.complete_ocean_edge_min, c.partial_ocean_edge_min,
                  ctx.params.elevation_post.edge_ocean_width);
              !s.ok()) {
            return failed(std::move(s));
          }
        } else if (classify::is_elevation_ranges(result.check_id) && !bands_done) {
          bands_done = true;
          core::Expected<bool> fitted = fit_bands(ctx, *z);
          if (!fitted) {
            return failed(std::move(fitted.error()));
          }
          if (!*fitted) {
            return result;
          }
        } else {
          return result;
        }
        result = check(ctx, *z);
      }
      return result;
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
