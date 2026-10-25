#include "wg/geology/minerals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "wg/core/distributions.hpp"

namespace wg::geology {

namespace {

constexpr std::size_t kMaxRows = std::numeric_limits<std::uint16_t>::max();

// Rock class labels in params::RockClass order, as the mineral table names them.
constexpr std::array<std::string_view, 4> kRockClassNames = {"sedimentary", "igneous_extrusive",
                                                             "igneous_intrusive", "metamorphic"};

core::InclusionType inclusion_type(params::Inclusion i) noexcept {
  switch (i) {
    case params::Inclusion::vein:
      return core::InclusionType::vein;
    case params::Inclusion::cluster:
      return core::InclusionType::cluster;
    case params::Inclusion::small_cluster:
      return core::InclusionType::cluster_small;
    case params::Inclusion::single:
      break;
  }
  return core::InclusionType::cluster_one;
}

core::Status unknown_host(const std::string& mineral, const std::string& host) noexcept {
  return core::make_status(core::StatusCode::invalid_argument,
                           "MineralHosts: mineral " + mineral + " names the unknown host " + host);
}

}  // namespace

core::Expected<MineralHosts> MineralHosts::create(const params::MineralTable& minerals,
                                                  const params::RockTable& rocks) noexcept {
  if (minerals.minerals.size() > kMaxRows || rocks.rocks.size() > kMaxRows) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "MineralHosts: a table has more than 65535 rows");
  }
  try {
    MineralHosts h;
    h.rock_entries_.resize(rocks.rocks.size());
    h.mineral_entries_.resize(minerals.minerals.size());
    for (std::size_t m = 0; m < minerals.minerals.size(); ++m) {
      const params::Mineral& mineral = minerals.minerals[m];
      for (const params::MineralEnvironment& env : mineral.environments) {
        const HostEntry entry{.mineral = static_cast<std::uint16_t>(m),
                              .inclusion = inclusion_type(env.inclusion),
                              .frequency = env.frequency};
        bool found = false;
        switch (env.host_kind) {
          case params::HostKind::rock_class:
            for (std::size_t r = 0; r < rocks.rocks.size(); ++r) {
              const auto c = static_cast<std::size_t>(rocks.rocks[r].rock_class);
              if (c < kRockClassNames.size() && kRockClassNames[c] == env.host) {
                h.rock_entries_[r].push_back(entry);
                found = true;
              }
            }
            // A class without rocks is no error.
            for (const std::string_view name : kRockClassNames) {
              found = found || name == env.host;
            }
            break;
          case params::HostKind::rock:
            for (std::size_t r = 0; r < rocks.rocks.size(); ++r) {
              if (rocks.rocks[r].name == env.host) {
                h.rock_entries_[r].push_back(entry);
                found = true;
              }
            }
            break;
          case params::HostKind::mineral:
            for (std::size_t k = 0; k < minerals.minerals.size(); ++k) {
              if (minerals.minerals[k].name == env.host) {
                h.mineral_entries_[k].push_back(entry);
                found = true;
              }
            }
            break;
        }
        if (!found) {
          return core::make_error(unknown_host(mineral.name, env.host));
        }
      }
    }
    return h;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "MineralHosts: no memory");
  }
}

double MineralHosts::rock_frequency(std::uint16_t rock) const noexcept {
  double sum = 0.0;
  for (const HostEntry& e : in_rock(rock)) {
    sum += static_cast<double>(e.frequency);
  }
  return sum;
}

double expected_deposits(const MineralHosts& hosts, std::uint16_t rock, std::size_t entry,
                         std::int64_t area_cells, double thickness_z,
                         std::int32_t scarcity) noexcept {
  const std::span<const HostEntry> entries = hosts.in_rock(rock);
  if (entry >= entries.size() || scarcity <= 0) {
    return 0.0;
  }
  const double share = static_cast<double>(entries[entry].frequency) / hosts.rock_frequency(rock);
  return kDepositRate * share * static_cast<double>(area_cells) * thickness_z /
         static_cast<double>(scarcity);
}

core::Expected<std::vector<core::MineralDeposit>> place_minerals(
    const MineralHosts& hosts, std::span<const core::GeoColumn> columns, const GeoRegions& regions,
    double hu_m, std::int32_t scarcity, core::RngKey key) noexcept {
  if (columns.size() != regions.size() || regions.offsets.size() != regions.size() + 1) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "place_minerals: one column per region needed");
  }
  if (!(hu_m > 0.0) || !std::isfinite(hu_m)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "place_minerals: hu_m must be positive");
  }
  const core::RngKey k = key.derive("minerals");
  try {
    std::vector<core::MineralDeposit> deposits;
    for (std::size_t r = 0; r < regions.size(); ++r) {
      const core::GeoColumn& column = columns[r];
      if (column.geo_region != static_cast<std::int32_t>(r) || column.layers.size() > kMaxRows) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "place_minerals: columns must be in region order");
      }
      const std::span<const std::int64_t> cells = regions.cells_of(r);
      const auto area = static_cast<std::int64_t>(cells.size());
      core::RngStream s(k, static_cast<std::uint64_t>(r));
      for (std::size_t l = 0; l < column.layers.size(); ++l) {
        const core::RockLayer& layer = column.layers[l];
        const double thickness_z = static_cast<double>(layer.thickness_m) / hu_m;
        const std::span<const HostEntry> entries = hosts.in_rock(layer.rock_id);
        for (std::size_t e = 0; e < entries.size(); ++e) {
          const std::uint64_t n = core::poisson(
              s, expected_deposits(hosts, layer.rock_id, e, area, thickness_z, scarcity));
          for (std::uint64_t d = 0; d < n; ++d) {
            const std::int64_t cell = cells[static_cast<std::size_t>(std::min(
                static_cast<std::int64_t>(s.uniform01() * static_cast<double>(area)), area - 1))];
            const auto host_layer = static_cast<std::uint16_t>(l);
            deposits.push_back({.mineral_id = entries[e].mineral,
                                .host_layer = host_layer,
                                .inclusion = entries[e].inclusion,
                                .cell = cell});
            for (const HostEntry& nested : hosts.in_mineral(entries[e].mineral)) {
              const std::uint64_t m =
                  core::poisson(s, static_cast<double>(nested.frequency) / 100.0);
              for (std::uint64_t j = 0; j < m; ++j) {
                deposits.push_back({.mineral_id = nested.mineral,
                                    .host_layer = host_layer,
                                    .inclusion = nested.inclusion,
                                    .cell = cell});
              }
            }
          }
        }
      }
    }
    return deposits;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "place_minerals: no memory");
  }
}

}  // namespace wg::geology
