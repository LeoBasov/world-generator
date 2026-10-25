#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/geology/regions.hpp"
#include "wg/params/tables.hpp"

namespace wg::geology {

// Stone column of R10.1 and R10.4 (A21): a constrained Markov chain over the rock classes with
// log-normal thicknesses, the soil layers, the three cavern layers and the magma sea. The
// conventions are recorded in docs/adr/0019-geology-subsurface.md.

/// Number of rock classes (params::RockClass).
inline constexpr std::size_t kRockClassCount = 4;

/// Number of cavern layers (R10.1).
inline constexpr std::size_t kCavernLevels = 3;

/// The bottom (intrusive) layer is this many times a drawn thickness: "the deepest and usually
/// thickest layer" (R10.1).
inline constexpr double kBottomThicknessFactor = 2.0;

/// The rock and soil tables, indexed for drawing columns: the rocks of every class (the
/// sedimentary ones also by ocean depth) and the soils of every environment, as table ids
/// ascending.
class GeologyTables {
 public:
  /// Fails with invalid_argument for a rock class without rocks or a table with more than 65535
  /// rows, out_of_memory.
  static core::Expected<GeologyTables> create(const params::RockTable& rocks,
                                              const params::SoilTable& soils) noexcept;

  /// Rocks of class c that may form a layer in environment e: under an ocean, sedimentary rocks
  /// need the matching ocean flag; every other combination has every rock of the class.
  [[nodiscard]] std::span<const std::uint16_t> rocks(params::RockClass c,
                                                     GeoEnvironment e) const noexcept;
  /// Soils of environment e: the land, sand_desert, shallow_ocean or deep_ocean flag; none on
  /// mountains.
  [[nodiscard]] std::span<const std::uint16_t> soils(GeoEnvironment e) const noexcept {
    return soils_[static_cast<std::size_t>(e)];
  }

  [[nodiscard]] std::size_t rock_count() const noexcept { return rock_class_.size(); }
  [[nodiscard]] std::size_t soil_count() const noexcept { return soil_aquifer_.size(); }
  /// Class of a rock id (< rock_count()).
  [[nodiscard]] params::RockClass rock_class(std::uint16_t rock) const noexcept {
    return rock_class_[rock];
  }
  /// AQUIFER flag of a rock or soil id.
  [[nodiscard]] bool rock_aquifer(std::uint16_t rock) const noexcept {
    return rock_aquifer_[rock] != 0;
  }
  [[nodiscard]] bool soil_aquifer(std::uint16_t soil) const noexcept {
    return soil_aquifer_[soil] != 0;
  }

 private:
  GeologyTables() = default;

  std::array<std::vector<std::uint16_t>, kRockClassCount> rocks_;
  std::vector<std::uint16_t> shallow_sedimentary_;
  std::vector<std::uint16_t> deep_sedimentary_;
  std::array<std::vector<std::uint16_t>, kGeoEnvironmentCount> soils_;
  std::vector<params::RockClass> rock_class_;
  std::vector<std::uint8_t> rock_aquifer_;
  std::vector<std::uint8_t> soil_aquifer_;
};

/// What a column depends on.
struct ColumnInputs {
  /// Geological region; keys the random streams.
  std::int32_t region = 0;
  GeoEnvironment environment = GeoEnvironment::land;
  /// Mean volcanism V of the region: P(extrusive top) = V / 100.
  double volcanism = 0.0;
  /// The region holds a volcano: the top layer is extrusive.
  bool volcano = false;
  /// Mean soil depth of the region [m], shared out among the soil layers.
  double soil_depth_m = 0.0;
  /// Metres per z-level (one elevation unit, grid.hu_m).
  double hu_m = 1.0;
};

/// Draws the column of one region (plan A21).
///
/// - Stone, from RngStream(key.derive("column"), region): L uniform in [layer_count_min,
///   layer_count_max]; the top is extrusive with probability V / 100 (1 with a volcano), else
///   sedimentary with sedimentary_top_probability, else metamorphic; each further layer is a
///   uniform choice among the successors S -> {S, M, I}, E -> {E, M, I}, M -> {M, I},
///   I -> {I, M}; the last layer is intrusive. Sedimentary needs a rock allowed in the
///   environment. The rock is uniform among its class's rocks, the thickness log_normal(mu,
///   sigma) z-levels times hu_m (kBottomThicknessFactor times for the last layer).
/// - Soil, from RngStream(key.derive("soil"), region): none on mountains, without soil depth or
///   without an eligible soil; else n uniform in [1, soil_max_layers] layers of uniform soils,
///   whose thicknesses are uniform shares of soil_depth_m.
/// - Caverns, from RngStream(key.derive("caverns"), region): openness, passage density and
///   water of level k uniform in their [MIN, MAX]; a level exists if its passage density is
///   positive. It lies at soil + stone (k + 1) / 4 below the surface, the magma sea at
///   soil + stone.
///
/// Errors: invalid_argument for layer counts outside [2, ...] or reversed, a negative sigma, a
/// cavern range outside [0, 100] or reversed, or hu_m not positive; out_of_memory.
[[nodiscard]] core::Expected<core::GeoColumn> draw_column(const GeologyTables& tables,
                                                          const params::GeologyParams& params,
                                                          const ColumnInputs& in,
                                                          core::RngKey key) noexcept;

/// Checks the rules of a column against the tables: at least two stone layers; ids in range;
/// positive finite thicknesses; the top not intrusive and the bottom intrusive; no sedimentary
/// layer together with an extrusive one; no metamorphic layer above a sedimentary one; caverns
/// by ascending level, below the surface and above the magma sea. Returns invalid_argument
/// naming the first broken rule.
[[nodiscard]] core::Status check_column(const core::GeoColumn& column,
                                        const GeologyTables& tables) noexcept;

}  // namespace wg::geology
