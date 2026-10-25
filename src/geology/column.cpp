#include "wg/geology/column.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "wg/core/distributions.hpp"

namespace wg::geology {

namespace {

using params::RockClass;

constexpr std::uint16_t kMaxRows = std::numeric_limits<std::uint16_t>::max();

// Uniform integer in [lo, hi] from one draw of s.
std::int32_t uniform_int(core::RngStream& s, std::int32_t lo, std::int32_t hi) noexcept {
  const auto n = static_cast<double>(hi - lo + 1);
  const auto k = static_cast<std::int32_t>(s.uniform01() * n);
  return lo + std::min(k, hi - lo);
}

// Uniform element of a non-empty list from one draw of s.
std::uint16_t pick(core::RngStream& s, std::span<const std::uint16_t> list) noexcept {
  return list[static_cast<std::size_t>(
      uniform_int(s, 0, static_cast<std::int32_t>(list.size()) - 1))];
}

core::Status invalid(const std::string& message) noexcept {
  return core::make_status(core::StatusCode::invalid_argument, "draw_column: " + message);
}

core::Status check_params(const params::GeologyParams& p, const ColumnInputs& in) noexcept {
  if (p.layer_count_min < 2 || p.layer_count_max < p.layer_count_min) {
    return invalid("layer counts must satisfy 2 <= layer_count_min <= layer_count_max");
  }
  if (!(p.layer_thickness_log_sigma >= 0.0) || !std::isfinite(p.layer_thickness_log_mean)) {
    return invalid("the thickness distribution needs a finite mean and sigma >= 0");
  }
  if (p.soil_max_layers < 0) {
    return invalid("soil_max_layers must not be negative");
  }
  if (!(in.hu_m > 0.0) || !std::isfinite(in.hu_m)) {
    return invalid("hu_m must be positive");
  }
  for (std::size_t k = 0; k < kCavernLevels; ++k) {
    for (const auto& [lo, hi] :
         {std::pair{p.cavern_layer_openness_min[k], p.cavern_layer_openness_max[k]},
          std::pair{p.cavern_layer_passage_density_min[k], p.cavern_layer_passage_density_max[k]},
          std::pair{p.cavern_layer_water_min[k], p.cavern_layer_water_max[k]}}) {
      if (lo < 0 || hi > 100 || lo > hi) {
        return invalid("cavern ranges must lie in [0, 100] with MIN <= MAX");
      }
    }
  }
  return {};
}

// Successor classes of a layer, in a fixed order (S may be removed by the caller).
std::span<const RockClass> successors(RockClass c) noexcept {
  static constexpr std::array<RockClass, 3> kAfterS = {
      RockClass::sedimentary, RockClass::metamorphic, RockClass::igneous_intrusive};
  static constexpr std::array<RockClass, 3> kAfterE = {
      RockClass::igneous_extrusive, RockClass::metamorphic, RockClass::igneous_intrusive};
  static constexpr std::array<RockClass, 2> kAfterM = {RockClass::metamorphic,
                                                       RockClass::igneous_intrusive};
  static constexpr std::array<RockClass, 2> kAfterI = {RockClass::igneous_intrusive,
                                                       RockClass::metamorphic};
  switch (c) {
    case RockClass::sedimentary:
      return kAfterS;
    case RockClass::igneous_extrusive:
      return kAfterE;
    case RockClass::metamorphic:
      return kAfterM;
    case RockClass::igneous_intrusive:
      break;
  }
  return kAfterI;
}

}  // namespace

core::Expected<GeologyTables> GeologyTables::create(const params::RockTable& rocks,
                                                    const params::SoilTable& soils) noexcept {
  if (rocks.rocks.size() > kMaxRows || soils.soils.size() > kMaxRows) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "GeologyTables: a table has more than 65535 rows");
  }
  try {
    GeologyTables t;
    for (std::size_t i = 0; i < rocks.rocks.size(); ++i) {
      const params::RockType& r = rocks.rocks[i];
      const auto id = static_cast<std::uint16_t>(i);
      const auto c = static_cast<std::size_t>(r.rock_class);
      if (c >= kRockClassCount) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "GeologyTables: rock class out of range");
      }
      t.rocks_[c].push_back(id);
      if (r.rock_class == RockClass::sedimentary) {
        if (r.shallow_ocean) {
          t.shallow_sedimentary_.push_back(id);
        }
        if (r.deep_ocean) {
          t.deep_sedimentary_.push_back(id);
        }
      }
      t.rock_class_.push_back(r.rock_class);
      t.rock_aquifer_.push_back(r.aquifer ? 1 : 0);
    }
    for (std::size_t c = 0; c < kRockClassCount; ++c) {
      if (t.rocks_[c].empty()) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "GeologyTables: every rock class needs at least one rock");
      }
    }
    for (std::size_t i = 0; i < soils.soils.size(); ++i) {
      const params::SoilType& s = soils.soils[i];
      const auto id = static_cast<std::uint16_t>(i);
      auto add = [&](GeoEnvironment e) { t.soils_[static_cast<std::size_t>(e)].push_back(id); };
      if (s.deep_ocean) {
        add(GeoEnvironment::deep_ocean);
      }
      if (s.shallow_ocean) {
        add(GeoEnvironment::shallow_ocean);
      }
      if (s.land) {
        add(GeoEnvironment::land);
      }
      if (s.sand_desert) {
        add(GeoEnvironment::sand_desert);
      }
      t.soil_aquifer_.push_back(s.aquifer ? 1 : 0);
    }
    return t;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "GeologyTables: no memory");
  }
}

std::span<const std::uint16_t> GeologyTables::rocks(RockClass c, GeoEnvironment e) const noexcept {
  if (c == RockClass::sedimentary) {
    if (e == GeoEnvironment::deep_ocean) {
      return deep_sedimentary_;
    }
    if (e == GeoEnvironment::shallow_ocean) {
      return shallow_sedimentary_;
    }
  }
  return rocks_[static_cast<std::size_t>(c)];
}

core::Expected<core::GeoColumn> draw_column(const GeologyTables& tables,
                                            const params::GeologyParams& params,
                                            const ColumnInputs& in, core::RngKey key) noexcept {
  if (core::Status s = check_params(params, in); !s.ok()) {
    return core::make_error(std::move(s));
  }
  try {
    core::GeoColumn column;
    column.geo_region = in.region;
    const auto stream_id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(in.region));

    // Stone.
    core::RngStream s(key.derive("column"), stream_id);
    const std::int32_t count = uniform_int(s, params.layer_count_min, params.layer_count_max);
    const bool allow_sedimentary = !tables.rocks(RockClass::sedimentary, in.environment).empty();
    const double p_extrusive =
        in.volcano ? 1.0
                   : std::clamp(std::isfinite(in.volcanism) ? in.volcanism / 100.0 : 0.0, 0.0, 1.0);
    RockClass c = RockClass::metamorphic;
    if (s.uniform01() < p_extrusive) {
      c = RockClass::igneous_extrusive;
    } else if (s.uniform01() < params.sedimentary_top_probability && allow_sedimentary) {
      c = RockClass::sedimentary;
    }
    double stone_m = 0.0;
    column.layers.reserve(static_cast<std::size_t>(count));
    for (std::int32_t k = 0; k < count; ++k) {
      if (k == count - 1) {
        c = RockClass::igneous_intrusive;
      } else if (k > 0) {
        const std::span<const RockClass> next = successors(c);
        c = next[static_cast<std::size_t>(
            uniform_int(s, 0, static_cast<std::int32_t>(next.size()) - 1))];
      }
      const std::uint16_t rock = pick(s, tables.rocks(c, in.environment));
      double z_levels =
          core::log_normal(s, params.layer_thickness_log_mean, params.layer_thickness_log_sigma);
      if (k == count - 1) {
        z_levels *= kBottomThicknessFactor;
      }
      const auto thickness = static_cast<float>(z_levels * in.hu_m);
      column.layers.push_back({.rock_id = rock, .thickness_m = thickness});
      stone_m += static_cast<double>(thickness);
    }

    // Soil.
    double soil_m = 0.0;
    const std::span<const std::uint16_t> soils = tables.soils(in.environment);
    if (in.environment != GeoEnvironment::mountain && in.soil_depth_m > 0.0 &&
        std::isfinite(in.soil_depth_m) && params.soil_max_layers > 0 && !soils.empty()) {
      core::RngStream ss(key.derive("soil"), stream_id);
      const std::int32_t n = uniform_int(ss, 1, params.soil_max_layers);
      std::vector<double> weights;
      weights.reserve(static_cast<std::size_t>(n));
      double total = 0.0;
      for (std::int32_t k = 0; k < n; ++k) {
        column.soil_layers.push_back({.soil_id = pick(ss, soils), .thickness_m = 0.0F});
        weights.push_back(ss.uniform_open01());
        total += weights.back();
      }
      for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto thickness = static_cast<float>(in.soil_depth_m * weights[k] / total);
        column.soil_layers[k].thickness_m = thickness;
        soil_m += static_cast<double>(thickness);
      }
    }

    // Caverns and the magma sea.
    core::RngStream cs(key.derive("caverns"), stream_id);
    for (std::size_t k = 0; k < kCavernLevels; ++k) {
      const std::int32_t openness =
          uniform_int(cs, params.cavern_layer_openness_min[k], params.cavern_layer_openness_max[k]);
      const std::int32_t passages = uniform_int(cs, params.cavern_layer_passage_density_min[k],
                                                params.cavern_layer_passage_density_max[k]);
      const std::int32_t water =
          uniform_int(cs, params.cavern_layer_water_min[k], params.cavern_layer_water_max[k]);
      if (passages > 0) {
        const double depth = soil_m + (stone_m * static_cast<double>(k + 1) /
                                       static_cast<double>(kCavernLevels + 1));
        column.caverns.push_back({.level = static_cast<std::uint8_t>(k),
                                  .depth_m = static_cast<float>(depth),
                                  .openness = static_cast<std::uint8_t>(openness),
                                  .passage_density = static_cast<std::uint8_t>(passages),
                                  .water = static_cast<std::uint8_t>(water)});
      }
    }
    column.magma_depth_m = static_cast<float>(soil_m + stone_m);
    return column;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "draw_column: no memory");
  }
}

core::Status check_column(const core::GeoColumn& column, const GeologyTables& tables) noexcept {
  auto broken = [](const char* rule) {
    return core::make_status(core::StatusCode::invalid_argument,
                             std::string("check_column: ") + rule);
  };
  const std::vector<core::RockLayer>& layers = column.layers;
  if (layers.size() < 2) {
    return broken("fewer than two stone layers");
  }
  bool sedimentary = false;
  bool extrusive = false;
  bool metamorphic = false;
  for (const core::RockLayer& l : layers) {
    if (l.rock_id >= tables.rock_count()) {
      return broken("rock id out of range");
    }
    if (!(l.thickness_m > 0.0F) || !std::isfinite(l.thickness_m)) {
      return broken("stone thickness not positive");
    }
    switch (tables.rock_class(l.rock_id)) {
      case RockClass::sedimentary:
        if (metamorphic) {
          return broken("metamorphic layer above a sedimentary layer");
        }
        sedimentary = true;
        break;
      case RockClass::igneous_extrusive:
        extrusive = true;
        break;
      case RockClass::metamorphic:
        metamorphic = true;
        break;
      case RockClass::igneous_intrusive:
        break;
    }
  }
  if (sedimentary && extrusive) {
    return broken("sedimentary and extrusive layers in one column");
  }
  if (tables.rock_class(layers.front().rock_id) == RockClass::igneous_intrusive) {
    return broken("intrusive top layer");
  }
  if (tables.rock_class(layers.back().rock_id) != RockClass::igneous_intrusive) {
    return broken("bottom layer not intrusive");
  }
  for (const core::SoilLayer& l : column.soil_layers) {
    if (l.soil_id >= tables.soil_count()) {
      return broken("soil id out of range");
    }
    if (!(l.thickness_m >= 0.0F) || !std::isfinite(l.thickness_m)) {
      return broken("soil thickness negative");
    }
  }
  for (std::size_t k = 0; k < column.caverns.size(); ++k) {
    const core::Cavern& v = column.caverns[k];
    if (v.level >= kCavernLevels || (k > 0 && v.level <= column.caverns[k - 1].level)) {
      return broken("cavern levels not ascending");
    }
    if (!(v.depth_m > 0.0F) || !(v.depth_m < column.magma_depth_m)) {
      return broken("cavern outside the stone");
    }
  }
  return {};
}

}  // namespace wg::geology
