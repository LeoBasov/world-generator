#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/geology/column.hpp"

namespace wg::geology {

// Aquifers of R10.3 (A21). The conventions are recorded in docs/adr/0019-geology-subsurface.md.

/// Values of the aquifer field (field registry enum).
enum class AquiferClass : std::uint8_t { none, light, heavy };

/// Chance that a heavy aquifer is heavy without the DF quirk: "about 1 in 20" (R10.3).
inline constexpr double kHeavyAquiferChance = 0.05;

/// True if a soil or stone layer of the column carries the AQUIFER flag.
[[nodiscard]] bool column_has_aquifer(const core::GeoColumn& column,
                                      const GeologyTables& tables) noexcept;

/// Chance of an aquifer on a land cell: (1 - D / 100) (1 - clamp((z - 100) / 200, 0, 1)), so
/// aquifers favour low ground and low drainage and vanish toward the mountains. D is clamped to
/// [0, 100]; NaN gives 0.
[[nodiscard]] double aquifer_probability(float elevation, float drainage) noexcept;

/// Cell inputs of assign_aquifers.
struct AquiferInputs {
  std::span<const float> elevation{};
  std::span<const float> drainage{};
  std::span<const std::uint8_t> environment{};  ///< GeoEnvironment values
  std::span<const std::int32_t> geo_region{};
  /// Per region: 1 if its column has an aquifer layer (column_has_aquifer).
  std::span<const std::uint8_t> region_has_aquifer{};
};

/// Aquifer class of every cell. Ocean and mountain cells and cells of a region without an
/// aquifer layer get none. Any other cell gets an aquifer if uniform01(key.derive("aquifer"),
/// cell, 0) < aquifer_probability; it is heavy if floor(D) mod 20 = 7 with heavy_quirk
/// (geology.df_heavy_aquifer_quirk), else if uniform01(..., cell, 1) < kHeavyAquiferChance.
/// Errors: invalid_argument for an invalid grid, a span without one value per cell or a region
/// out of range; cancelled.
[[nodiscard]] core::Status assign_aquifers(const AquiferInputs& in, const core::Grid& grid,
                                           bool heavy_quirk, core::RngKey key,
                                           std::span<std::uint8_t> aquifer, core::Executor& exec,
                                           const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::geology
