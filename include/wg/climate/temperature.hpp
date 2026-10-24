#pragma once

#include <algorithm>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::climate {

// Temperature model of R5.3 (A14) on the game scale (peak summer):
//   T = T_0 + T_phi(phi) - Gamma max(0, z - z_sea) h_u - alpha V_veg + delta T_P.
// The latitude term T_phi is latitude_term in latitude.hpp. The conventions are recorded in
// docs/adr/0012-temperature-model.md.

/// Lapse-rate term -k Gamma max(0, z - z_sea) h_u / 1000 [game], with Gamma = lapse_rate in
/// K/km, h_u = hu_m in metres per elevation unit and k = game_per_kelvin. Zero at and below sea
/// level (z_sea = terrain::kSeaLevel).
[[nodiscard]] inline double lapse_term(double z, double lapse_rate, double hu_m,
                                       double game_per_kelvin) noexcept {
  const double per_unit = game_per_kelvin * lapse_rate * hu_m / 1000.0;
  return -per_unit * std::max(0.0, z - static_cast<double>(terrain::kSeaLevel));
}

/// Forest damping -alpha V_veg [game] for vegetation V_veg in [0, 1].
[[nodiscard]] inline double vegetation_term(double vegetation, double alpha) noexcept {
  return -alpha * vegetation;
}

/// Wet-cell damping delta T_P = -rain_damping P / 100 [game] for rainfall P in [0, 100].
[[nodiscard]] inline double rain_term(double rainfall, double rain_damping) noexcept {
  return -rain_damping * rainfall / 100.0;
}

/// Game-scale value of a temperature in degC: k c + celsius_offset (R5.2).
[[nodiscard]] inline double celsius_to_game(double celsius,
                                            const params::TemperatureParams& p) noexcept {
  return p.game_per_kelvin * celsius + p.celsius_offset;
}

/// Inverse of celsius_to_game.
[[nodiscard]] inline double game_to_celsius(double game,
                                            const params::TemperatureParams& p) noexcept {
  return (game - p.celsius_offset) / p.game_per_kelvin;
}

/// Per-cell inputs of compute_temperature, one value per cell each. An empty optional span
/// leaves its term out.
struct TemperatureInputs {
  std::span<const float> base{};        ///< T_0 [game] (required)
  std::span<const float> latitude{};    ///< phi [rad]; empty: no latitude term
  std::span<const float> elevation{};   ///< z [elevation units]; empty: no lapse term
  std::span<const float> vegetation{};  ///< V_veg in [0, 1]; empty: no forest damping
  std::span<const float> rainfall{};    ///< P in [0, 100]; empty: no rain damping
};

/// Writes T for every cell into out: the terms summed in float64 in the order base, latitude,
/// lapse, vegetation, rain, clamped to the registry range of temperature and stored as float32.
/// h_u is grid.hu_m. out may be in.base (in place), so a later stage can add the remaining
/// terms to a temperature that already holds the latitude term. Errors: invalid_argument for an
/// invalid grid or a span without one value per cell; those of core::parallel_for, cancelled
/// included.
[[nodiscard]] core::Status compute_temperature(const core::Grid& grid, const TemperatureInputs& in,
                                               const params::TemperatureParams& p,
                                               std::span<float> out, core::Executor& exec,
                                               const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::climate
