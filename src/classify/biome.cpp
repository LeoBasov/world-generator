#include "wg/classify/biome.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "numerics/common.hpp"
#include "wg/hydro/water.hpp"

namespace wg::classify {

namespace {

namespace detail = num::detail;
using params::BiomeId;

constexpr std::uint8_t kNoIndex = 0xFF;
constexpr BiomeId kNoBiome = 0xFFFF;
constexpr std::int32_t kIndexMax = 100;

core::Unexpected invalid(std::string_view message) noexcept {
  return core::make_error(core::StatusCode::invalid_argument, message);
}

// floor(v) clamped to [lo, hi]; NaN gives lo.
std::int32_t floor_clamped(double v, std::int32_t lo, std::int32_t hi) noexcept {
  if (!(v >= lo)) {
    return lo;
  }
  if (v >= hi) {
    return hi;
  }
  return static_cast<std::int32_t>(std::floor(v));
}

std::size_t index_of(const std::vector<std::string>& names, std::string_view name) noexcept {
  const auto it = std::find(names.begin(), names.end(), name);
  return static_cast<std::size_t>(it - names.begin());
}

std::size_t band_index(const std::vector<params::TemperatureBand>& bands,
                       std::string_view name) noexcept {
  const auto it = std::find_if(bands.begin(), bands.end(),
                               [&](const params::TemperatureBand& b) { return b.name == name; });
  return static_cast<std::size_t>(it - bands.begin());
}

// Lake or river lookup: per band and salinity value s, the row listing the largest class value
// <= s. Every band needs a row listing 0, so that every s resolves.
bool fill_water_lut(const std::vector<params::WaterBiome>& rows,
                    const std::vector<params::TemperatureBand>& bands, std::size_t biome_count,
                    std::vector<BiomeId>& lut) {
  constexpr std::size_t kValues = 256;
  lut.assign(bands.size() * kValues, kNoBiome);
  std::vector<std::int32_t> owner(bands.size() * kValues, -1);
  for (const params::WaterBiome& row : rows) {
    if (row.biome >= biome_count) {
      return false;
    }
    for (const std::string& name : row.bands) {
      const std::size_t band = band_index(bands, name);
      if (band == bands.size()) {
        return false;
      }
      for (const std::int32_t v : row.salinity) {
        if (v < 0 || v > 255) {
          return false;
        }
        for (auto s = static_cast<std::size_t>(v); s < kValues; ++s) {
          const std::size_t k = (band * kValues) + s;
          if (v > owner[k]) {
            owner[k] = v;
            lut[k] = row.biome;
          }
        }
      }
    }
  }
  return std::none_of(lut.begin(), lut.end(), [](BiomeId b) { return b == kNoBiome; });
}

}  // namespace

core::Expected<BiomeClassifier> BiomeClassifier::create(const params::BiomeTable& table) noexcept {
  try {
    BiomeClassifier c;
    c.thresholds_ = table.thresholds;
    const std::size_t biome_count = table.biomes.size();
    const auto known = [&](BiomeId b) { return b < biome_count; };

    // Temperature bands.
    const std::vector<params::TemperatureBand>& bands = table.temperature_bands;
    if (bands.empty() || bands.size() >= kNoIndex) {
      return invalid("BiomeClassifier: need 1 to 254 temperature bands");
    }
    c.band_count_ = bands.size();
    c.band_lut_.assign(kTempValues, kNoIndex);
    c.frozen_.assign(bands.size(), 0);
    for (std::size_t b = 0; b < bands.size(); ++b) {
      c.frozen_[b] = bands[b].frozen ? 1 : 0;
      for (std::int32_t t = std::max(bands[b].min, kTempMin); t <= std::min(bands[b].max, kTempMax);
           ++t) {
        std::uint8_t& slot = c.band_lut_[static_cast<std::size_t>(t - kTempMin)];
        if (slot != kNoIndex) {
          return invalid("BiomeClassifier: temperature bands overlap");
        }
        slot = static_cast<std::uint8_t>(b);
      }
    }
    if (std::find(c.band_lut_.begin(), c.band_lut_.end(), kNoIndex) != c.band_lut_.end()) {
      return invalid("BiomeClassifier: temperature bands leave a gap in [-1000, 1000]");
    }

    // Biomes and region types.
    if (table.region_types.empty() || table.region_types.size() > 256) {
      return invalid("BiomeClassifier: need 1 to 256 region types");
    }
    c.region_type_count_ = table.region_types.size();
    c.biome_region_type_.assign(biome_count, 0);
    for (std::size_t i = 0; i < biome_count; ++i) {
      const params::Biome& biome = table.biomes[i];
      const std::size_t type = index_of(table.region_types, biome.region_type);
      if (biome.id != i || type == table.region_types.size()) {
        return invalid("BiomeClassifier: biome with a wrong id or an unknown region type");
      }
      c.biome_region_type_[i] = static_cast<std::uint8_t>(type);
    }
    if (!known(table.mountain) || !known(table.tundra) || !known(table.glacier)) {
      return invalid("BiomeClassifier: unknown special biome");
    }
    c.mountain_ = table.mountain;
    c.tundra_ = table.tundra;
    c.glacier_ = table.glacier;

    // Base rows.
    if (table.base.empty() || table.base.size() >= kNoIndex) {
      return invalid("BiomeClassifier: need 1 to 254 base rows");
    }
    std::vector<std::string> base_names;
    bool any_hills = false;
    c.base_lut_.assign(kIndexValues * kIndexValues, kNoIndex);
    for (std::size_t r = 0; r < table.base.size(); ++r) {
      const params::BaseBiomeRow& row = table.base[r];
      const std::size_t base = index_of(base_names, row.base);
      if (base == base_names.size()) {
        base_names.push_back(row.base);
        c.base_wetland_.push_back(row.wetland ? 1 : 0);
      }
      c.row_base_.push_back(static_cast<std::uint8_t>(base));
      c.row_hills_.push_back(row.hills ? 1 : 0);
      any_hills = any_hills || row.hills;
      for (std::int32_t p = std::max(row.rainfall_min, 0);
           p <= std::min(row.rainfall_max, kIndexMax); ++p) {
        for (std::int32_t d = std::max(row.drainage_min, 0);
             d <= std::min(row.drainage_max, kIndexMax); ++d) {
          std::uint8_t& slot = c.base_lut_[(static_cast<std::size_t>(p) * kIndexValues) +
                                           static_cast<std::size_t>(d)];
          if (slot != kNoIndex) {
            return invalid("BiomeClassifier: base rows overlap");
          }
          slot = static_cast<std::uint8_t>(r);
        }
      }
    }
    if (std::find(c.base_lut_.begin(), c.base_lut_.end(), kNoIndex) != c.base_lut_.end()) {
      return invalid("BiomeClassifier: base rows leave a (rainfall, drainage) pair uncovered");
    }
    if (any_hills) {
      const std::size_t hills = index_of(table.region_types, "hills");
      if (hills == table.region_types.size()) {
        return invalid("BiomeClassifier: base rows with hills need the region type hills");
      }
      c.hills_type_ = static_cast<std::uint8_t>(hills);
    }

    // Variants: (base, band, saline) -> biome; rows without saline fill both slots.
    c.variant_lut_.assign(base_names.size() * bands.size() * 2, kNoBiome);
    for (const params::BiomeVariant& v : table.variants) {
      const std::size_t base = index_of(base_names, v.base);
      if (base == base_names.size() || !known(v.biome)) {
        return invalid("BiomeClassifier: variant with an unknown base or biome");
      }
      for (const std::string& name : v.bands) {
        const std::size_t band = band_index(bands, name);
        if (band == bands.size()) {
          return invalid("BiomeClassifier: variant with an unknown band");
        }
        const std::size_t k = ((base * bands.size()) + band) * 2;
        if (!v.has_saline || !v.saline) {
          c.variant_lut_[k] = v.biome;
        }
        if (!v.has_saline || v.saline) {
          c.variant_lut_[k + 1] = v.biome;
        }
      }
    }
    for (std::size_t base = 0; base < base_names.size(); ++base) {
      for (std::size_t band = 0; band < bands.size(); ++band) {
        const std::size_t k = ((base * bands.size()) + band) * 2;
        const bool missing = c.variant_lut_[k] == kNoBiome ||
                             (c.base_wetland_[base] != 0 && c.variant_lut_[k + 1] == kNoBiome);
        if (!bands[band].frozen && missing) {
          return invalid("BiomeClassifier: a base row has no variant in a band");
        }
      }
    }

    // Mangrove.
    c.mangrove_base_ = index_of(base_names, table.mangrove.base);
    c.mangrove_band_ = band_index(bands, table.mangrove.band);
    if (c.mangrove_base_ == base_names.size() || c.mangrove_band_ == bands.size() ||
        !known(table.mangrove.biome)) {
      return invalid("BiomeClassifier: mangrove rule with an unknown base, band or biome");
    }
    c.mangrove_ = table.mangrove.biome;

    // Water biomes.
    c.ocean_lut_.assign(bands.size(), kNoBiome);
    for (const params::WaterBiome& row : table.ocean) {
      if (!known(row.biome)) {
        return invalid("BiomeClassifier: ocean row with an unknown biome");
      }
      for (const std::string& name : row.bands) {
        const std::size_t band = band_index(bands, name);
        if (band == bands.size()) {
          return invalid("BiomeClassifier: ocean row with an unknown band");
        }
        c.ocean_lut_[band] = row.biome;
      }
    }
    if (std::find(c.ocean_lut_.begin(), c.ocean_lut_.end(), kNoBiome) != c.ocean_lut_.end()) {
      return invalid("BiomeClassifier: a band has no ocean biome");
    }
    if (!fill_water_lut(table.lakes, bands, biome_count, c.lake_lut_)) {
      return invalid("BiomeClassifier: a band or salinity has no lake biome");
    }
    if (!fill_water_lut(table.rivers, bands, biome_count, c.river_lut_)) {
      return invalid("BiomeClassifier: a band or salinity has no river biome");
    }
    return c;
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "BiomeClassifier: no memory");
  }
}

std::size_t BiomeClassifier::band_of(double t) const noexcept {
  return band_lut_[static_cast<std::size_t>(floor_clamped(t, kTempMin, kTempMax) - kTempMin)];
}

params::BiomeId BiomeClassifier::water_biome(const std::vector<params::BiomeId>& lut,
                                             std::size_t band, std::uint8_t s) const noexcept {
  return lut[(band * kSalinityValues) + s];
}

params::BiomeId BiomeClassifier::river_biome(double t, std::uint8_t s) const noexcept {
  return water_biome(river_lut_, band_of(t), s);
}

CellClass BiomeClassifier::classify(const CellInputs& in) const noexcept {
  const std::size_t band = band_of(static_cast<double>(in.temperature));
  const double z = std::floor(static_cast<double>(in.elevation));
  const auto with_type = [&](BiomeId b) { return CellClass{b, biome_region_type_[b]}; };

  // 1. Water.
  if (in.has_water) {
    if (in.water == static_cast<std::uint8_t>(hydro::WaterClass::ocean)) {
      return with_type(ocean_lut_[band]);
    }
    if (in.water == static_cast<std::uint8_t>(hydro::WaterClass::lake)) {
      return with_type(water_biome(lake_lut_, band, in.salinity));
    }
  } else if (z <= thresholds_.ocean_max_elevation) {
    return with_type(ocean_lut_[band]);
  }
  // 2. Mountain, 3. freezing.
  if (z >= thresholds_.mountain_min_elevation) {
    return with_type(mountain_);
  }
  const std::int32_t d = floor_clamped(static_cast<double>(in.drainage), 0, kIndexMax);
  if (frozen_[band] != 0) {
    return with_type(d < thresholds_.glacier_min_drainage ? tundra_ : glacier_);
  }
  // 4. Base row, 5. temperature and salinity variants.
  const std::int32_t p = floor_clamped(static_cast<double>(in.rainfall), 0, kIndexMax);
  const std::size_t row =
      base_lut_[(static_cast<std::size_t>(p) * kIndexValues) + static_cast<std::size_t>(d)];
  const std::size_t base = row_base_[row];
  const bool saline = in.salinity >= thresholds_.saline_min_salinity;
  const std::size_t slot = (base_wetland_[base] != 0 && saline) ? 1 : 0;
  BiomeId biome = variant_lut_[(((base * band_count_) + band) * 2) + slot];
  if (base == mangrove_base_ && band == mangrove_band_ && saline &&
      d <= thresholds_.mangrove_max_drainage) {
    biome = mangrove_;
  }
  return {biome, row_hills_[row] != 0 ? hills_type_ : biome_region_type_[biome]};
}

core::Status classify_biomes(const core::Grid& grid, const BiomeInputs& in,
                             const BiomeClassifier& classifier, std::span<params::BiomeId> biome,
                             std::span<std::uint8_t> region_type, core::Executor& exec,
                             const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(in.elevation, grid,
                           "classify_biomes: elevation must have one value per cell"),
       detail::check_field(in.rainfall, grid,
                           "classify_biomes: rainfall must have one value per cell"),
       detail::check_field(in.drainage, grid,
                           "classify_biomes: drainage must have one value per cell"),
       detail::check_field(in.temperature, grid,
                           "classify_biomes: temperature must have one value per cell"),
       detail::check_optional_field(in.salinity, grid,
                                    "classify_biomes: salinity must have one value per cell"),
       detail::check_optional_field(in.water, grid,
                                    "classify_biomes: water must have one value per cell"),
       detail::check_field(biome, grid, "classify_biomes: biome must have one value per cell"),
       detail::check_optional_field(region_type, grid,
                                    "classify_biomes: region_type must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const std::int64_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      const CellClass c =
          classifier.classify({.elevation = in.elevation[k],
                               .rainfall = in.rainfall[k],
                               .drainage = in.drainage[k],
                               .temperature = in.temperature[k],
                               .salinity = in.salinity.empty() ? std::uint8_t{0} : in.salinity[k],
                               .water = in.water.empty() ? std::uint8_t{0} : in.water[k],
                               .has_water = !in.water.empty()});
      biome[k] = c.biome;
      if (!region_type.empty()) {
        region_type[k] = c.region_type;
      }
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel);
}

}  // namespace wg::classify
