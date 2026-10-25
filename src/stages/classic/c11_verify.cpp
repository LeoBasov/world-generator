// C11 Verification (S11, A22): the final REGION_COUNTS ([2], regions per type) and SUBREGION_MAX
// on the regions of C9 (R11). The stage writes nothing; ADR 0018 and ADR 0020 record the rules.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "stages/classic/classic_stages.hpp"
#include "wg/classify/checks.hpp"
#include "wg/classify/regions.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::LayerId;
using pipeline::StageContext;
using pipeline::StageResult;

class C11Verify final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC11Verify; }
  [[nodiscard]] FieldSet inputs() const noexcept override {
    return {FieldId::region_id, LayerId::regions};
  }
  [[nodiscard]] FieldSet outputs() const noexcept override { return {}; }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c11_verify: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<const params::DataTables*> tables = data_tables(ctx, kC11Verify);
    if (!tables) {
      return failed(std::move(tables.error()));
    }
    core::Expected<std::span<const std::int32_t>> region_id =
        read_field<std::int32_t>(ctx, FieldId::region_id);
    if (!region_id) {
      return failed(std::move(region_id.error()));
    }
    const std::vector<core::Region>& regions = ctx.state.layers().regions;

    // Region type per cell, from the region the cell belongs to.
    std::vector<std::uint8_t> types(region_id->size());
    for (std::size_t i = 0; i < types.size(); ++i) {
      const std::int32_t id = (*region_id)[i];
      if (id < 0 || std::cmp_greater_equal(id, regions.size())) {
        return StageResult::error(core::make_status(
            core::StatusCode::invalid_argument,
            "c11_verify: region_id " + std::to_string(id) + " is not in the regions layer"));
      }
      types[i] = static_cast<std::uint8_t>(regions[static_cast<std::size_t>(id)].type);
    }
    const params::BiomeTable& table = (*tables)->biomes;
    core::Expected<std::vector<classify::RegionTypeCount>> counts =
        classify::count_region_types(types, regions, table.region_types.size());
    if (!counts) {
      return failed(std::move(counts.error()));
    }

    std::optional<classify::RegionCheckFailure> failure =
        classify::check_region_counts(*counts, table.region_types, ctx.params.checks.region_counts,
                                      classify::RegionCountPhase::final);
    if (!failure) {
      failure = classify::check_subregion_max(static_cast<std::int64_t>(regions.size()),
                                              ctx.params.checks.subregion_max);
    }
    StageResult result =
        failure ? StageResult::reject(failure->check_id, failure->measured, failure->required)
                : StageResult::ok();
    result.metrics.add("regions", static_cast<double>(regions.size()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c11_verify() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C11Verify());
}

}  // namespace wg::stages
