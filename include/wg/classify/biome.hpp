#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/params/tables.hpp"

namespace wg::classify {

// Biome classification of R9.1-R9.2 (A20): a pure function of the local fields, driven by the
// biome table (data/tables/biome_table.json). The conventions are recorded in
// docs/adr/0018-biomes-regions.md.

/// Inputs of one cell.
struct CellInputs {
  float elevation = 0.0F;     ///< z [elevation units]
  float rainfall = 0.0F;      ///< P in [0, 100]
  float drainage = 0.0F;      ///< D in [0, 100]
  float temperature = 0.0F;   ///< T [game scale]
  std::uint8_t salinity = 0;  ///< salinity class value; 0 (fresh) if not known
  std::uint8_t water = 0;     ///< hydro::WaterClass, used only if has_water
  bool has_water = false;     ///< false: ocean comes from the elevation, lakes are not known
};

/// Biome of a cell and its region type (index into BiomeTable::region_types).
struct CellClass {
  params::BiomeId biome = 0;
  std::uint8_t region_type = 0;

  friend bool operator==(const CellClass&, const CellClass&) = default;
};

/// The biome table compiled into lookup tables. Every value is rounded down before it is
/// compared with the table's integer bounds; rainfall and drainage are clamped to [0, 100] and
/// temperature to [-1000, 1000] (NaN counts as the lower bound).
class BiomeClassifier {
 public:
  /// Compiles table. Errors: invalid_argument if a temperature in [-1000, 1000] has no or two
  /// bands, a (rainfall, drainage) pair no or two base rows, a base row in a band (and salinity,
  /// for wetlands) no variant, a band no ocean biome or no lake or river biome for salinity 0,
  /// a biome id or region type is unknown, there are more than 256 region types, or a base row
  /// has hills but there is no region type "hills"; out_of_memory.
  [[nodiscard]] static core::Expected<BiomeClassifier> create(
      const params::BiomeTable& table) noexcept;

  /// Class of one cell by R9.1, in this order:
  /// 1. water: with has_water, an ocean cell gets the ocean biome of its band and a lake cell
  ///    the lake biome of its band and salinity; without water, z <= ocean_max_elevation is
  ///    ocean. River cells are classified as land.
  /// 2. z >= mountain_min_elevation: mountain.
  /// 3. a frozen band: tundra if D < glacier_min_drainage, else glacier.
  /// 4. the base row of (P, D), then its variant in the band; wetlands also by saline
  ///    (salinity >= saline_min_salinity). The mangrove rule replaces the variant for its base
  ///    and band when the cell is saline and D <= mangrove_max_drainage.
  /// The region type is the biome's, except "hills" for a land cell of a base row with hills.
  [[nodiscard]] CellClass classify(const CellInputs& in) const noexcept;

  /// Biome of a river cell with temperature t and salinity class s (for export and the local
  /// map; classify never returns it).
  [[nodiscard]] params::BiomeId river_biome(double t, std::uint8_t s) const noexcept;

  /// Index into BiomeTable::temperature_bands of temperature t.
  [[nodiscard]] std::size_t band_of(double t) const noexcept;

  /// Region type of biome b (b must be a biome id of the table).
  [[nodiscard]] std::uint8_t region_type_of(params::BiomeId b) const noexcept {
    return biome_region_type_[b];
  }

  /// Number of region types.
  [[nodiscard]] std::size_t region_type_count() const noexcept { return region_type_count_; }

 private:
  static constexpr std::int32_t kTempMin = -1000;
  static constexpr std::int32_t kTempMax = 1000;
  static constexpr std::size_t kTempValues = 2001;  // kTempMin ... kTempMax
  static constexpr std::size_t kIndexValues = 101;  // rainfall, drainage in 0 ... 100
  static constexpr std::size_t kSalinityValues = 256;

  BiomeClassifier() noexcept = default;

  [[nodiscard]] params::BiomeId water_biome(const std::vector<params::BiomeId>& lut,
                                            std::size_t band, std::uint8_t s) const noexcept;

  params::BiomeThresholds thresholds_;
  std::vector<std::uint8_t> band_lut_;           // floor(T) - kTempMin -> band
  std::vector<std::uint8_t> frozen_;             // per band
  std::vector<std::uint8_t> base_lut_;           // P * 101 + D -> base row
  std::vector<std::uint8_t> row_base_;           // base row -> base label index
  std::vector<std::uint8_t> row_hills_;          // per base row
  std::vector<std::uint8_t> base_wetland_;       // per base label
  std::vector<params::BiomeId> variant_lut_;     // (base * bands + band) * 2 + saline -> biome
  std::vector<params::BiomeId> ocean_lut_;       // band -> biome
  std::vector<params::BiomeId> lake_lut_;        // band * 256 + s -> biome
  std::vector<params::BiomeId> river_lut_;       // band * 256 + s -> biome
  std::vector<std::uint8_t> biome_region_type_;  // biome -> region type
  std::size_t band_count_ = 0;
  std::size_t region_type_count_ = 0;
  std::size_t mangrove_base_ = 0;
  std::size_t mangrove_band_ = 0;
  params::BiomeId mangrove_ = 0;
  params::BiomeId mountain_ = 0;
  params::BiomeId tundra_ = 0;
  params::BiomeId glacier_ = 0;
  std::uint8_t hills_type_ = 0;
};

/// Per-cell inputs of classify_biomes, one value per cell each.
struct BiomeInputs {
  std::span<const float> elevation{};        ///< z (required)
  std::span<const float> rainfall{};         ///< P (required)
  std::span<const float> drainage{};         ///< D (required)
  std::span<const float> temperature{};      ///< T [game scale] (required)
  std::span<const std::uint8_t> salinity{};  ///< salinity class; empty: fresh everywhere
  std::span<const std::uint8_t> water{};     ///< hydro::WaterClass; empty: no water field
};

/// Writes BiomeClassifier::classify of every cell into biome and, if not empty, its region type
/// into region_type. Each cell depends only on its own inputs, so the result does not depend on
/// the thread count. Errors: invalid_argument for an invalid grid or a span without one value
/// per cell; those of core::parallel_for, cancelled included.
[[nodiscard]] core::Status classify_biomes(const core::Grid& grid, const BiomeInputs& in,
                                           const BiomeClassifier& classifier,
                                           std::span<params::BiomeId> biome,
                                           std::span<std::uint8_t> region_type,
                                           core::Executor& exec,
                                           const core::CancelFlag* cancel = nullptr) noexcept;

}  // namespace wg::classify
