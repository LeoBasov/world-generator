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
inline constexpr std::string_view kC05Shape = "c05_shape";
inline constexpr std::string_view kC07Smooth = "c07_smooth";

/// C1 Allocate and orient (S1): writes latitude from the pole option (R5.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c01_allocate() noexcept;

/// C2 Seed primary fields (S2): the six fractal fields (R3), the latitude bias of temperature
/// (R5.3) and the peak candidates (R4.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c02_seed() noexcept;

/// C3 Fit and first check (S3): the hypsometric remap (R4.1), the edge-ocean and peak checks
/// (R11) and the edge-ocean repair.
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c03_fit() noexcept;

/// C5 Terrain shaping (S5): plains smoothing (R4.4), volcano placement and VOLCANO_MIN (R4.5).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c05_shape() noexcept;

/// C7 Final smoothing (S7): peak and volcano bumps (R4.3, R4.5), land smoothing and the
/// downstream constraint along the rivers (plan 7.1).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c07_smooth() noexcept;

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
