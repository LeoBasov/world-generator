#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/geology/regions.hpp"
#include "wg/params/tables.hpp"

namespace wg::geology {

// Mineral deposits of R10.2 and R10.4 (A21): a Poisson process per host layer,
//   E[N_k] = c f_k / sum_j f_j  V_h / scarcity,
// with V_h the layer volume in tile z-levels. Recorded in docs/adr/0019-geology-subsurface.md.

/// The constant c of E[N_k].
inline constexpr double kDepositRate = 1.0;

/// One mineral environment that matches a host.
struct HostEntry {
  std::uint16_t mineral = 0;
  core::InclusionType inclusion = core::InclusionType::vein;
  std::int32_t frequency = 1;

  friend bool operator==(const HostEntry&, const HostEntry&) = default;
};

/// The mineral table indexed by host: for every rock the environments that name its class or
/// the rock itself, and for every mineral those that name it as host (nested inclusions), each
/// in table order (minerals, then their environments).
class MineralHosts {
 public:
  /// Fails with invalid_argument for a host rock, class or mineral that does not exist or a
  /// table with more than 65535 rows; out_of_memory.
  static core::Expected<MineralHosts> create(const params::MineralTable& minerals,
                                             const params::RockTable& rocks) noexcept;

  [[nodiscard]] std::span<const HostEntry> in_rock(std::uint16_t rock) const noexcept {
    return rock < rock_entries_.size() ? std::span<const HostEntry>(rock_entries_[rock])
                                       : std::span<const HostEntry>();
  }
  [[nodiscard]] std::span<const HostEntry> in_mineral(std::uint16_t mineral) const noexcept {
    return mineral < mineral_entries_.size() ? std::span<const HostEntry>(mineral_entries_[mineral])
                                             : std::span<const HostEntry>();
  }
  /// Sum of the frequencies of in_rock(rock).
  [[nodiscard]] double rock_frequency(std::uint16_t rock) const noexcept;

 private:
  MineralHosts() = default;

  std::vector<std::vector<HostEntry>> rock_entries_;
  std::vector<std::vector<HostEntry>> mineral_entries_;
};

/// Expected number of deposits of entry in_rock(rock)[entry] in a layer of thickness_z
/// z-levels over area_cells cells: kDepositRate f / sum f area_cells thickness_z / scarcity.
/// 0 for an entry out of range or a scarcity <= 0.
[[nodiscard]] double expected_deposits(const MineralHosts& hosts, std::uint16_t rock,
                                       std::size_t entry, std::int64_t area_cells,
                                       double thickness_z, std::int32_t scarcity) noexcept;

/// Places the deposits of every region r (columns[r].geo_region must be r) from
/// RngStream(key.derive("minerals"), r): for every stone layer and every entry of its rock,
/// N = poisson(expected_deposits) deposits (thickness_z = thickness_m / hu_m), each on a uniform
/// cell of the region; after each deposit, every entry nested in its mineral adds
/// poisson(f / 100) deposits on the same cell and layer (one level deep). Errors:
/// invalid_argument for columns that do not match the regions or hu_m not positive;
/// out_of_memory.
[[nodiscard]] core::Expected<std::vector<core::MineralDeposit>> place_minerals(
    const MineralHosts& hosts, std::span<const core::GeoColumn> columns, const GeoRegions& regions,
    double hu_m, std::int32_t scarcity, core::RngKey key) noexcept;

}  // namespace wg::geology
