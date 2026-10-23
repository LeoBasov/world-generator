#pragma once

#include "wg/core/status.hpp"
#include "wg/pipeline/registry.hpp"

namespace wg::stages {

// Stages of the classic profile (plan section 4.3), one per source file in src/stages/classic/;
// a stage id equals its file stem and never changes, because it keys the RNG (R1).
//
//   c01_allocate  reads nothing     writes latitude                                (A12)
//   c02_seed      reads latitude    writes the six primary fields, peaks           (A12)

/// Adds every classic stage implemented so far to registry. Errors as StageRegistry::add
/// (already_exists if a stage is registered twice), out_of_memory.
[[nodiscard]] core::Status add_classic_stages(pipeline::StageRegistry& registry) noexcept;

}  // namespace wg::stages
