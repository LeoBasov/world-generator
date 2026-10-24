#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::bio {

// Vegetation of R8.1 (A19): net primary productivity from the Miami model (Lieth, 1975),
//   NPP = min(3000 / (1 + e^(1.315 - 0.119 T)), 3000 (1 - e^(-0.000664 P))),
// with T in degC and P in mm/yr, normalised to V_veg = NPP / 3000. The conventions are recorded
// in docs/adr/0017-vegetation-salinity.md.

/// Upper bound of the Miami model and normalisation of V_veg [g m^-2 yr^-1].
inline constexpr double kNppMax = 3000.0;

/// Highest game-scale temperature of the freezing biomes (R9.1).
inline constexpr double kIceTemperatureMax = -5.0;

/// Lowest drainage at which a freezing cell is glacier rather than tundra (R9.1).
inline constexpr double kIceDrainageMin = 75.0;

/// Temperature limit 3000 / (1 + e^(1.315 - 0.119 T)) [g m^-2 yr^-1] for T in degC.
[[nodiscard]] double miami_npp_temperature(double celsius) noexcept;

/// Precipitation limit 3000 (1 - e^(-0.000664 P)) [g m^-2 yr^-1] for P in mm/yr.
[[nodiscard]] double miami_npp_precipitation(double rain_mm) noexcept;

/// NPP of the Miami model: the smaller of the two limits [g m^-2 yr^-1].
[[nodiscard]] double miami_npp(double celsius, double rain_mm) noexcept;

/// True on ice: a glacier cell of R9.1, game-scale temperature <= kIceTemperatureMax and
/// drainage >= kIceDrainageMin.
[[nodiscard]] constexpr bool is_ice(double temperature, double drainage) noexcept {
  return temperature <= kIceTemperatureMax && drainage >= kIceDrainageMin;
}

/// Per-cell inputs of compute_vegetation, one value per cell each.
struct VegetationInputs {
  std::span<const float> temperature{};   ///< T [game scale] (required)
  std::span<const float> rainfall{};      ///< P in [0, 100] (required)
  std::span<const float> elevation{};     ///< z [elevation units] (required)
  std::span<const float> drainage{};      ///< D in [0, 100] (required)
  std::span<const std::uint8_t> water{};  ///< hydro::WaterClass; empty: lakes are not known
};

/// Writes V_veg = NPP / 3000 for every cell into out, with T = climate::game_to_celsius of the
/// temperature and P_mm = rain_mm_per_index P. V_veg is 0 on ocean (z < terrain::kSeaLevel),
/// on lake cells of water, above the tree line (z > tree_line) and on ice (is_ice). The value
/// is computed in float64, clamped to the registry range [0, 1] and stored as float32. out must
/// not overlap the inputs. Each cell depends only on its own inputs, so the result does not
/// depend on the thread count. Errors: invalid_argument for an invalid grid or a span without
/// one value per cell; those of core::parallel_for, cancelled included.
[[nodiscard]] core::Status compute_vegetation(const core::Grid& grid, const VegetationInputs& in,
                                              const params::VegetationParams& p,
                                              const params::TemperatureParams& temperature,
                                              std::span<float> out, core::Executor& exec,
                                              const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::bio
