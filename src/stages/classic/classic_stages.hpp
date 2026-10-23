#pragma once

// Factories of the classic stages (internal); add_classic_stages registers them.

#include <memory>
#include <string_view>
#include <utility>

#include "wg/core/status.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::stages {

inline constexpr std::string_view kC01Allocate = "c01_allocate";
inline constexpr std::string_view kC02Seed = "c02_seed";

/// C1 Allocate and orient (S1): writes latitude from the pole option (R5.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c01_allocate() noexcept;

/// C2 Seed primary fields (S2): the six fractal fields (R3), the latitude bias of temperature
/// (R5.3) and the peak candidates (R4.3).
[[nodiscard]] std::unique_ptr<pipeline::Stage> make_c02_seed() noexcept;

/// Result of a stage whose kernels failed with status: cancelled for cancelled, error otherwise.
[[nodiscard]] inline pipeline::StageResult failed(core::Status status) noexcept {
  if (status.code == core::StatusCode::cancelled) {
    return pipeline::StageResult::cancelled();
  }
  return pipeline::StageResult::error(std::move(status));
}

}  // namespace wg::stages
