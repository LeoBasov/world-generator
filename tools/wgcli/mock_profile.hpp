#pragma once

// Mock profile of wgcli (A10): the stages of tests/fixtures/profiles/mock.json, so the commands
// can run a profile end to end before the first real stage exists (A11). They live in the tool,
// not in the library, to keep test stages out of the public API.
//
//   m01_seed    reads nothing                 writes latitude
//   m02_shape   reads latitude                writes elevation, peaks
//   m03_verify  reads elevation, peaks        writes biome; rejects with check MOCK_VERIFY
//                                             when uniform01(key, 0, 0) < kMockRejectRate

#include <cstdint>

#include "wg/core/status.hpp"
#include "wg/pipeline/registry.hpp"

namespace wg::cli {

/// Share of attempts m03_verify rejects, decided by its RNG key alone.
inline constexpr double kMockRejectRate = 0.25;

/// Adds m01_seed, m02_shape and m03_verify. Errors as StageRegistry::add.
core::Status add_mock_stages(pipeline::StageRegistry& registry) noexcept;

/// True if m03_verify rejects attempt a of seed (lets tests pick seeds that reject).
[[nodiscard]] bool mock_verify_rejects(std::uint64_t seed, std::uint32_t attempt) noexcept;

}  // namespace wg::cli
