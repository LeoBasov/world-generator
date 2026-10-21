#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/params/validation.hpp"

namespace wg::params {

// Data tables (R3 requirement: thresholds are data, not code). Each table is a JSON file with
// a schema_version and a JSON Schema in schema/; data/tables/ holds the biome, climate and
// salinity tables, data/geology/ the rock, soil and mineral tables (plan section 8). Loaders
// check structure and the rules of each table and refuse a table with errors (invalid_argument,
// one "<path>: <message>" line per error); load_tables() adds the rules across tables.

/// Evidence level of a table row (report section 1, plan 16).
enum class Confidence : std::uint8_t { documented, observed, unverified };

// ---------------------------------------------------------------------------
// Climate and salinity classes
// ---------------------------------------------------------------------------

/// One temperature class of R5.2 on the game scale.
struct ClimateClass {
  std::string name;
  double min_value = 0.0;  ///< lowest game-scale temperature of the class (inclusive)
  double celsius_min = 0.0;
  double celsius_max = 0.0;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const ClimateClass&, const ClimateClass&) = default;
};

/// Climate classes sorted by min_value; the first starts at -1000.
struct ClimateClasses {
  std::vector<ClimateClass> classes;

  /// Index of the last class whose min_value is <= temperature (0 below the first).
  [[nodiscard]] std::size_t classify(double temperature) const noexcept;

  friend bool operator==(const ClimateClasses&, const ClimateClasses&) = default;
};

/// One salinity class of R8.2.
struct SalinityClass {
  std::string name;
  std::int32_t value = 0;     ///< stored class value (0, 33, 66, 100)
  double min_salinity = 0.0;  ///< lowest S(d) of the class (inclusive)
  Confidence confidence = Confidence::observed;

  friend bool operator==(const SalinityClass&, const SalinityClass&) = default;
};

/// Salinity classes sorted by value; their values equal the registry values of the field.
struct SalinityClasses {
  std::vector<SalinityClass> classes;

  /// Class value of a salinity S(d): the last class whose min_salinity is <= s.
  [[nodiscard]] std::int32_t quantise(double s) const noexcept;

  friend bool operator==(const SalinityClasses&, const SalinityClasses&) = default;
};

// ---------------------------------------------------------------------------
// Biome table (R9)
// ---------------------------------------------------------------------------

/// Biome id, the value of the biome field.
using BiomeId = std::uint16_t;

struct Biome {
  BiomeId id = 0;  ///< equals the position in BiomeTable::biomes; append-only
  std::string name;
  std::string region_type;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const Biome&, const Biome&) = default;
};

struct BiomeThresholds {
  std::int32_t ocean_max_elevation = 99;
  std::int32_t mountain_min_elevation = 300;
  std::int32_t glacier_min_drainage = 75;
  std::int32_t saline_min_salinity = 66;
  std::int32_t mangrove_max_drainage = 9;

  friend bool operator==(const BiomeThresholds&, const BiomeThresholds&) = default;
};

/// Inclusive range of the temperature rounded down.
struct TemperatureBand {
  std::string name;
  std::int32_t min = 0;
  std::int32_t max = 0;
  bool frozen = false;  ///< land becomes tundra or glacier
  Confidence confidence = Confidence::observed;

  friend bool operator==(const TemperatureBand&, const TemperatureBand&) = default;
};

/// Row of R9.2: a rectangle of rainfall x drainage with its base label.
struct BaseBiomeRow {
  std::string base;
  std::int32_t rainfall_min = 0;
  std::int32_t rainfall_max = 0;
  std::int32_t drainage_min = 0;
  std::int32_t drainage_max = 0;
  bool hills = false;
  bool wetland = false;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const BaseBiomeRow&, const BaseBiomeRow&) = default;
};

/// Biome of a base label in some temperature bands; wetland rows also by salinity.
struct BiomeVariant {
  std::string base;
  std::vector<std::string> bands;
  bool has_saline = false;  ///< wetland rows only
  bool saline = false;
  BiomeId biome = 0;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const BiomeVariant&, const BiomeVariant&) = default;
};

/// Biome of water cells in some temperature bands (and salinity classes for lakes, rivers).
struct WaterBiome {
  std::vector<std::string> bands;
  std::vector<std::int32_t> salinity;  ///< class values; empty for the ocean
  BiomeId biome = 0;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const WaterBiome&, const WaterBiome&) = default;
};

struct MangroveRule {
  std::string base;
  std::string band;
  BiomeId biome = 0;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const MangroveRule&, const MangroveRule&) = default;
};

struct RegionSize {
  std::string name;
  std::int32_t min_tiles = 1;

  friend bool operator==(const RegionSize&, const RegionSize&) = default;
};

/// The decision table of R9.1-R9.2 used by the classification (A20).
struct BiomeTable {
  std::vector<std::string> region_types;
  std::vector<Biome> biomes;
  BiomeThresholds thresholds;
  std::vector<TemperatureBand> temperature_bands;
  std::vector<BaseBiomeRow> base;
  std::vector<BiomeVariant> variants;
  MangroveRule mangrove;
  BiomeId mountain = 0;
  BiomeId tundra = 0;
  BiomeId glacier = 0;
  std::vector<WaterBiome> ocean;
  std::vector<WaterBiome> lakes;
  std::vector<WaterBiome> rivers;
  std::vector<RegionSize> region_sizes;

  /// Biome with the given name, or nullptr.
  [[nodiscard]] const Biome* find(std::string_view name) const noexcept;

  friend bool operator==(const BiomeTable&, const BiomeTable&) = default;
};

// ---------------------------------------------------------------------------
// Geology (R10)
// ---------------------------------------------------------------------------

enum class RockClass : std::uint8_t {
  sedimentary,
  igneous_extrusive,
  igneous_intrusive,
  metamorphic
};

struct RockType {
  std::string name;
  RockClass rock_class = RockClass::sedimentary;
  bool aquifer = false;
  bool shallow_ocean = false;
  bool deep_ocean = false;
  double erodibility = 0.0;  ///< stream-power K [m^(1-2m)/yr], uncalibrated default
  Confidence confidence = Confidence::observed;

  friend bool operator==(const RockType&, const RockType&) = default;
};

struct RockTable {
  std::vector<RockType> rocks;

  /// Rock with the given name, or nullptr.
  [[nodiscard]] const RockType* find(std::string_view name) const noexcept;

  friend bool operator==(const RockTable&, const RockTable&) = default;
};

struct SoilType {
  std::string name;
  bool aquifer = false;
  double permeability = 0.0;  ///< k_soil in [0, 1]
  bool land = false;
  bool shallow_ocean = false;
  bool deep_ocean = false;
  bool sand_desert = false;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const SoilType&, const SoilType&) = default;
};

struct SoilTable {
  std::vector<SoilType> soils;

  friend bool operator==(const SoilTable&, const SoilTable&) = default;
};

enum class MineralKind : std::uint8_t { ore, fuel, gem, host };
enum class Inclusion : std::uint8_t { vein, cluster, small_cluster, single };
enum class HostKind : std::uint8_t { rock_class, rock, mineral };

/// Where a mineral occurs (DF ENVIRONMENT / ENVIRONMENT_SPEC).
struct MineralEnvironment {
  HostKind host_kind = HostKind::rock_class;
  std::string host;  ///< rock class name, rock name or mineral name
  Inclusion inclusion = Inclusion::vein;
  std::int32_t frequency = 1;  ///< 1..100

  friend bool operator==(const MineralEnvironment&, const MineralEnvironment&) = default;
};

struct Mineral {
  std::string name;
  MineralKind kind = MineralKind::ore;
  std::vector<MineralEnvironment> environments;
  Confidence confidence = Confidence::observed;

  friend bool operator==(const Mineral&, const Mineral&) = default;
};

struct MineralTable {
  std::vector<Mineral> minerals;

  friend bool operator==(const MineralTable&, const MineralTable&) = default;
};

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

/// All tables a world needs.
struct DataTables {
  BiomeTable biomes;
  ClimateClasses climate;
  SalinityClasses salinity;
  RockTable rocks;
  SoilTable soils;
  MineralTable minerals;

  friend bool operator==(const DataTables&, const DataTables&) = default;
};

/// Version of the table schemas this library reads.
inline constexpr int kTablesSchemaVersion = 1;

// Each loader takes the JSON text of one table. Errors: parse_error, unsupported_version for
// a newer schema_version, invalid_argument for a table with errors.
[[nodiscard]] core::Expected<BiomeTable> load_biome_table(std::string_view json) noexcept;
[[nodiscard]] core::Expected<ClimateClasses> load_climate_classes(std::string_view json) noexcept;
[[nodiscard]] core::Expected<SalinityClasses> load_salinity_classes(std::string_view json) noexcept;
[[nodiscard]] core::Expected<RockTable> load_rock_types(std::string_view json) noexcept;
[[nodiscard]] core::Expected<SoilTable> load_soils(std::string_view json) noexcept;
[[nodiscard]] core::Expected<MineralTable> load_minerals(std::string_view json) noexcept;

/// JSON text of every table, e.g. read through Godot's FileAccess (plan 12.1).
struct TableSources {
  std::string_view biome_table;
  std::string_view climate_classes;
  std::string_view salinity_classes;
  std::string_view rock_types;
  std::string_view soils;
  std::string_view minerals;
};

/// Loads every table and checks the rules across tables (validate_tables). The error of the
/// first table that fails names that table.
[[nodiscard]] core::Expected<DataTables> load_tables(const TableSources& sources) noexcept;

/// load_tables on the files of a data directory: tables/biome_table.json,
/// tables/climate_classes.json, tables/salinity_classes.json, geology/rock_types.json,
/// geology/soils.json and geology/minerals.json. Errors: io_error if a file cannot be read.
[[nodiscard]] core::Expected<DataTables> load_tables_dir(
    const std::filesystem::path& data_dir) noexcept;

/// Rules across tables: the lake and river salinities of the biome table are salinity class
/// values and cover every class in every band; every rock a mineral names exists. Paths are
/// prefixed with the table, e.g. "minerals:$.minerals[3].environments[0].host_rock".
[[nodiscard]] std::vector<ValidationIssue> validate_tables(const DataTables& tables) noexcept;

/// XXH3-64 of a canonical encoding of every table value (like params_hash, ADR 0005).
[[nodiscard]] std::uint64_t tables_hash(const DataTables& tables) noexcept;

}  // namespace wg::params
