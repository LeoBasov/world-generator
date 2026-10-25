#pragma once

// Factories of the classic stages (internal); add_classic_stages registers them.

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::stages {

inline constexpr std::string_view kC01Allocate = "c01_allocate";
inline constexpr std::string_view kC02Seed = "c02_seed";
inline constexpr std::string_view kC03Fit = "c03_fit";
inline constexpr std::string_view kC04Vegetation = "c04_vegetation";
inline constexpr std::string_view kC05Shape = "c05_shape";
inline constexpr std::string_view kC06Rivers = "c06_rivers";
inline constexpr std::string_view kC07Smooth = "c07_smooth";
inline constexpr std::string_view kC08Climate = "c08_climate";
inline constexpr std::string_view kC09Biomes = "c09_biomes";

/// C1 Allocate and orient (S1): writes latitude from the pole option (R5.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c01_allocate() noexcept;

/// C2 Seed primary fields (S2): the six fractal fields (R3), the latitude bias of temperature
/// (R5.3) and the peak candidates (R4.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c02_seed() noexcept;

/// C3 Fit and first check (S3): the hypsometric remap (R4.1), the edge-ocean and peak checks
/// (R11) and the edge-ocean repair.
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c03_fit() noexcept;

/// C4 First vegetation (S4): vegetation (R8.1), the provisional biome (R9.1-R9.2) and the
/// initial REGION_COUNTS (R11).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c04_vegetation() noexcept;

/// C5 Terrain shaping (S5): plains smoothing (R4.4), volcano placement and VOLCANO_MIN (R4.5).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c05_shape() noexcept;

/// C6 Erosion and rivers (S6): small oceans (R6.6), test-river erosion and RIVER_MINS (R6.1,
/// R6.3, R6.4), filling, routing, discharge (R6.5), lakes and lake bulges (R6.1) and the river
/// network with its width classes (R6.2).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c06_rivers() noexcept;

/// C7 Final smoothing (S7): peak and volcano bumps (R4.3, R4.5), land smoothing and the
/// downstream constraint along the rivers (plan 7.1).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c07_smooth() noexcept;

/// C8 Climate recomputation (S8): orographic rainfall (R7.2), temperature with the lapse, forest
/// and rain terms (R5.3), vegetation (R8.1), salinity and the lake salinity classes (R8.2).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c08_climate() noexcept;

/// C9 Biomes and regions (S9): the final biome (R9.1-R9.2) and the named regions (R9.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c09_biomes() noexcept;

/// The field id of the stage's state as T, for writing.
template <class T>
[[nodiscard]] core::Expected<std::span<T>> write_field(pipeline::StageContext& ctx,
                                                       core::FieldId id) noexcept {
  core::Expected<core::FieldView<T>> view = ctx.state.write<T>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return view->data;
}

/// The field id of the stage's state as T, for reading.
template <class T>
[[nodiscard]] core::Expected<std::span<const T>> read_field(const pipeline::StageContext& ctx,
                                                            core::FieldId id) noexcept {
  core::Expected<core::FieldView<const T>> view = ctx.state.read<T>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return view->data;
}

/// The data tables of the run, or invalid_argument naming stage if the run has none.
[[nodiscard]] inline core::Expected<const params::DataTables*> data_tables(
    const pipeline::StageContext& ctx, std::string_view stage) noexcept {
  if (ctx.tables == nullptr) {
    return core::make_error(core::make_status(core::StatusCode::invalid_argument,
                                              std::string(stage) + ": the run has no data tables"));
  }
  return ctx.tables;
}

/// The float32 field id of the stage's state, for writing.
[[nodiscard]] inline core::Expected<std::span<float>> write_float(pipeline::StageContext& ctx,
                                                                  core::FieldId id) noexcept {
  core::Expected<core::FieldView<float>> view = ctx.state.write<float>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return view->data;
}

/// Like write_float, for a field an earlier stage wrote: not_found if it is absent (write_float
/// would add it, zero-filled).
[[nodiscard]] inline core::Expected<std::span<float>> update_float(pipeline::StageContext& ctx,
                                                                   core::FieldId id) noexcept {
  if (!ctx.state.has(id)) {
    return core::make_error(core::make_status(
        core::StatusCode::not_found, std::string(core::field_info(id).name) + " is missing"));
  }
  return write_float(ctx, id);
}

/// Result of a stage whose kernels failed with status: cancelled for cancelled, error otherwise.
[[nodiscard]] inline pipeline::StageResult failed(core::Status status) noexcept {
  if (status.code == core::StatusCode::cancelled) {
    return pipeline::StageResult::cancelled();
  }
  return pipeline::StageResult::error(std::move(status));
}

}  // namespace wg::stages
