#include "wg/params/tables.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "encoder.hpp"
#include "io/json.hpp"
#include "reader.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::params {

namespace {

using detail::add_error;
using detail::format_number;
using detail::ObjectReader;
using Issues = std::vector<ValidationIssue>;

// ---------------------------------------------------------------------------
// Reading helpers
// ---------------------------------------------------------------------------

bool is_name(std::string_view s, bool upper) noexcept {
  const char first_lo = upper ? 'A' : 'a';
  const char first_hi = upper ? 'Z' : 'z';
  if (s.empty() || s[0] < first_lo || s[0] > first_hi) {
    return false;
  }
  return std::all_of(s.begin(), s.end(), [&](char c) {
    return (c >= first_lo && c <= first_hi) || (c >= '0' && c <= '9') || c == '_';
  });
}

// A lower-case id ([a-z][a-z0-9_]*), or an upper-case one; "" with an issue otherwise.
std::string read_name(ObjectReader& r, std::string_view key, Issues& issues, bool upper = false) {
  std::optional<std::string> s = r.string(key);
  if (!s) {
    return {};
  }
  if (!is_name(*s, upper)) {
    add_error(
        issues, r.child(key),
        "\"" + *s + "\" is not a name (" + (upper ? "[A-Z][A-Z0-9_]*" : "[a-z][a-z0-9_]*") + ")");
    return {};
  }
  return *s;
}

template <class E>
E read_enum(ObjectReader& r, std::string_view key, const std::vector<std::string_view>& labels,
            E fallback) {
  const std::optional<std::string> s = r.label(key, labels);
  if (!s) {
    return fallback;
  }
  const auto it = std::find(labels.begin(), labels.end(), std::string_view(*s));
  return static_cast<E>(it - labels.begin());
}

const std::vector<std::string_view>& confidence_labels() {
  static const std::vector<std::string_view> labels = {"documented", "observed", "unverified"};
  return labels;
}

const std::vector<std::string_view>& rock_class_labels() {
  static const std::vector<std::string_view> labels = {"sedimentary", "igneous_extrusive",
                                                       "igneous_intrusive", "metamorphic"};
  return labels;
}

Confidence read_confidence(ObjectReader& r) {
  return read_enum(r, "confidence", confidence_labels(), Confidence::observed);
}

bool read_flag(ObjectReader& r, std::string_view key) { return r.boolean(key).value_or(false); }

std::int32_t read_int(ObjectReader& r, std::string_view key, std::int32_t lo, std::int32_t hi) {
  return static_cast<std::int32_t>(r.integer(key, lo, hi).value_or(lo));
}

// Calls row(reader) for every object of the array at key.
template <class Row>
void for_each_row(ObjectReader& r, std::string_view key, Issues& issues, Row row) {
  const io::Json* a = r.array(key);
  if (a == nullptr) {
    return;
  }
  if (a->empty()) {
    add_error(issues, r.child(key), "must not be empty");
    return;
  }
  for (std::size_t i = 0; i < a->size(); ++i) {
    ObjectReader item((*a)[i], io::json_index_path(r.child(key), i), issues);
    if (item.valid()) {
      row(item);
      item.finish();
    }
  }
}

// The strings of the array at key; each must be a name.
std::vector<std::string> read_names(ObjectReader& r, std::string_view key, Issues& issues) {
  std::vector<std::string> out;
  const io::Json* a = r.array(key);
  if (a == nullptr) {
    return out;
  }
  if (a->empty()) {
    add_error(issues, r.child(key), "must not be empty");
  }
  for (std::size_t i = 0; i < a->size(); ++i) {
    const io::Json& v = (*a)[i];
    const std::string path = io::json_index_path(r.child(key), i);
    if (!v.is_string()) {
      detail::add_type_error(issues, path, "string", v);
    } else if (!is_name(v.get_ref<const std::string&>(), false)) {
      add_error(issues, path, "\"" + v.get<std::string>() + "\" is not a name ([a-z][a-z0-9_]*)");
    } else {
      out.push_back(v.get<std::string>());
    }
  }
  return out;
}

std::vector<std::int32_t> read_ints(ObjectReader& r, std::string_view key, std::int32_t lo,
                                    std::int32_t hi, Issues& issues) {
  std::vector<std::int32_t> out;
  const io::Json* a = r.array(key);
  if (a == nullptr) {
    return out;
  }
  if (a->empty()) {
    add_error(issues, r.child(key), "must not be empty");
  }
  for (std::size_t i = 0; i < a->size(); ++i) {
    if (const std::optional<double> v = detail::read_number(
            (*a)[i], io::json_index_path(r.child(key), i), lo, hi, true, issues)) {
      out.push_back(static_cast<std::int32_t>(*v));
    }
  }
  return out;
}

template <class T>
void check_unique_names(const std::vector<T>& rows, std::string_view array_path, Issues& issues) {
  std::set<std::string> seen;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (!seen.insert(rows[i].name).second) {
      add_error(issues, io::json_index_path(array_path, i) + ".name",
                "duplicate name \"" + rows[i].name + "\"");
    }
  }
}

std::string error_lines(std::string_view table, const Issues& issues) {
  std::string text = "invalid " + std::string(table) + ":";
  for (const ValidationIssue& i : issues) {
    if (i.severity == Severity::error) {
      text += "\n" + i.path + ": " + i.message;
    }
  }
  return text;
}

// Parses a table, checks its schema_version, reads it with read and, if that found no error,
// checks its rules with check.
template <class T, class Read, class Check>
core::Expected<T> load_table(std::string_view json, std::string_view table, Read read,
                             Check check) noexcept {
  core::Expected<io::Json> root = io::parse_json(json);
  if (!root) {
    return core::make_error(root.error().code, std::string(table) + ": " + root.error().message);
  }
  try {
    Issues issues;
    T out{};
    ObjectReader r(*root, "$", issues);
    if (r.valid()) {
      const std::optional<std::int64_t> version =
          r.integer("schema_version", 1, std::numeric_limits<std::int32_t>::max());
      if (version && *version > kTablesSchemaVersion) {
        return core::make_error(core::StatusCode::unsupported_version,
                                std::string(table) + ": schema_version " +
                                    std::to_string(*version) + " is newer than the supported " +
                                    std::to_string(kTablesSchemaVersion));
      }
      read(r, out, issues);
      r.finish();
      if (!has_errors(issues)) {
        check(out, issues);
      }
    }
    if (has_errors(issues)) {
      return core::make_error(core::StatusCode::invalid_argument, error_lines(table, issues));
    }
    return out;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            std::string(table) + ": reading failed: " + e.what());
  }
}

// ---------------------------------------------------------------------------
// Climate and salinity classes
// ---------------------------------------------------------------------------

void read_climate(ObjectReader& r, ClimateClasses& out, Issues& issues) {
  for_each_row(r, "classes", issues, [&](ObjectReader& c) {
    ClimateClass k;
    k.name = read_name(c, "name", issues);
    k.min_value = c.number("min_value", -1000.0, 1000.0).value_or(0.0);
    k.celsius_min = c.number("celsius_min", -273.15, 1000.0).value_or(0.0);
    k.celsius_max = c.number("celsius_max", -273.15, 1000.0).value_or(0.0);
    k.confidence = read_confidence(c);
    out.classes.push_back(std::move(k));
  });
}

void check_climate(const ClimateClasses& t, Issues& issues) {
  check_unique_names(t.classes, "$.classes", issues);
  const double t_min = core::field_info(core::FieldId::temperature).range.min;
  if (t.classes.front().min_value != t_min) {
    add_error(issues, "$.classes[0].min_value",
              "the first class must start at the temperature minimum " + format_number(t_min));
  }
  for (std::size_t i = 0; i < t.classes.size(); ++i) {
    const ClimateClass& c = t.classes[i];
    const std::string path = io::json_index_path("$.classes", i);
    if (i > 0 && !(c.min_value > t.classes[i - 1].min_value)) {
      add_error(issues, path + ".min_value", "classes must be sorted by increasing min_value");
    }
    if (c.celsius_min > c.celsius_max) {
      add_error(issues, path + ".celsius_min", "celsius_min is above celsius_max");
    }
  }
}

void read_salinity(ObjectReader& r, SalinityClasses& out, Issues& issues) {
  for_each_row(r, "classes", issues, [&](ObjectReader& c) {
    SalinityClass k;
    k.name = read_name(c, "name", issues);
    k.value = read_int(c, "value", 0, 100);
    k.min_salinity = c.number("min_salinity", 0.0, 100.0).value_or(0.0);
    k.confidence = read_confidence(c);
    out.classes.push_back(std::move(k));
  });
}

void check_salinity(const SalinityClasses& t, Issues& issues) {
  check_unique_names(t.classes, "$.classes", issues);
  const std::span<const double> values = core::field_info(core::FieldId::salinity).range.values;
  bool same = t.classes.size() == values.size();
  for (std::size_t i = 0; same && i < values.size(); ++i) {
    same = t.classes[i].value == values[i];
  }
  if (!same) {
    std::string list;
    for (double v : values) {
      list += (list.empty() ? "" : ", ") + format_number(v);
    }
    add_error(issues, "$.classes", "class values must be the salinity field values " + list);
  }
  if (t.classes.front().min_salinity != 0.0) {
    add_error(issues, "$.classes[0].min_salinity", "the first class must start at 0");
  }
  for (std::size_t i = 1; i < t.classes.size(); ++i) {
    if (!(t.classes[i].min_salinity > t.classes[i - 1].min_salinity)) {
      add_error(issues, io::json_index_path("$.classes", i) + ".min_salinity",
                "classes must be sorted by increasing min_salinity");
    }
  }
}

// ---------------------------------------------------------------------------
// Biome table
// ---------------------------------------------------------------------------

// Biome names are resolved to ids after the biome list is read.
struct BiomeRefs {
  std::vector<std::pair<std::string, std::string>> refs;  // (path, name) in reading order
  std::vector<BiomeId*> targets;

  void add(ObjectReader& r, std::string_view key, BiomeId* target, Issues& issues) {
    std::string name = read_name(r, key, issues, true);
    if (!name.empty()) {
      refs.emplace_back(r.child(key), std::move(name));
      targets.push_back(target);
    }
  }
};

void read_water(ObjectReader& r, std::string_view key, bool with_salinity,
                std::vector<WaterBiome>& out, BiomeRefs& refs, Issues& issues) {
  // Reserve so the BiomeId pointers handed to refs stay valid.
  if (const io::Json* a = r.array(key, 0, false); a != nullptr) {
    out.reserve(a->size());
  }
  for_each_row(r, key, issues, [&](ObjectReader& w) {
    WaterBiome& b = out.emplace_back();
    b.bands = read_names(w, "bands", issues);
    if (with_salinity) {
      b.salinity = read_ints(w, "salinity", 0, 100, issues);
    }
    refs.add(w, "biome", &b.biome, issues);
    b.confidence = read_confidence(w);
  });
}

void read_biomes(ObjectReader& r, BiomeTable& out, Issues& issues) {
  out.region_types = read_names(r, "region_types", issues);
  for_each_row(r, "biomes", issues, [&](ObjectReader& b) {
    Biome k;
    k.id = static_cast<BiomeId>(read_int(b, "id", 0, 65535));
    k.name = read_name(b, "name", issues, true);
    k.region_type = read_name(b, "region_type", issues);
    k.confidence = read_confidence(b);
    out.biomes.push_back(std::move(k));
  });
  if (const io::Json* t = r.object("thresholds")) {
    ObjectReader th(*t, r.child("thresholds"), issues);
    out.thresholds.ocean_max_elevation = read_int(th, "ocean_max_elevation", 0, 400);
    out.thresholds.mountain_min_elevation = read_int(th, "mountain_min_elevation", 0, 400);
    out.thresholds.glacier_min_drainage = read_int(th, "glacier_min_drainage", 0, 100);
    out.thresholds.saline_min_salinity = read_int(th, "saline_min_salinity", 0, 100);
    out.thresholds.mangrove_max_drainage = read_int(th, "mangrove_max_drainage", 0, 100);
    th.finish();
  }
  for_each_row(r, "temperature_bands", issues, [&](ObjectReader& b) {
    TemperatureBand k;
    k.name = read_name(b, "name", issues);
    if (const auto range = b.interval("range", -1000, 1000)) {
      k.min = static_cast<std::int32_t>(range->first);
      k.max = static_cast<std::int32_t>(range->second);
    }
    k.frozen = read_flag(b, "frozen");
    k.confidence = read_confidence(b);
    out.temperature_bands.push_back(std::move(k));
  });
  for_each_row(r, "base", issues, [&](ObjectReader& b) {
    BaseBiomeRow k;
    k.base = read_name(b, "base", issues);
    if (const auto p = b.interval("rainfall", 0, 100)) {
      k.rainfall_min = static_cast<std::int32_t>(p->first);
      k.rainfall_max = static_cast<std::int32_t>(p->second);
    }
    if (const auto d = b.interval("drainage", 0, 100)) {
      k.drainage_min = static_cast<std::int32_t>(d->first);
      k.drainage_max = static_cast<std::int32_t>(d->second);
    }
    k.hills = read_flag(b, "hills");
    k.wetland = read_flag(b, "wetland");
    k.confidence = read_confidence(b);
    out.base.push_back(std::move(k));
  });

  BiomeRefs refs;
  if (const io::Json* a = r.array("variants", 0, false); a != nullptr) {
    out.variants.reserve(a->size());
  }
  for_each_row(r, "variants", issues, [&](ObjectReader& v) {
    BiomeVariant& k = out.variants.emplace_back();
    k.base = read_name(v, "base", issues);
    k.bands = read_names(v, "bands", issues);
    if (const std::optional<bool> saline = v.boolean("saline", false)) {
      k.has_saline = true;
      k.saline = *saline;
    }
    refs.add(v, "biome", &k.biome, issues);
    k.confidence = read_confidence(v);
  });
  if (const io::Json* m = r.object("mangrove")) {
    ObjectReader mr(*m, r.child("mangrove"), issues);
    out.mangrove.base = read_name(mr, "base", issues);
    out.mangrove.band = read_name(mr, "band", issues);
    refs.add(mr, "biome", &out.mangrove.biome, issues);
    out.mangrove.confidence = read_confidence(mr);
    mr.finish();
  }
  if (const io::Json* s = r.object("special")) {
    ObjectReader sr(*s, r.child("special"), issues);
    refs.add(sr, "mountain", &out.mountain, issues);
    refs.add(sr, "tundra", &out.tundra, issues);
    refs.add(sr, "glacier", &out.glacier, issues);
    sr.finish();
  }
  read_water(r, "ocean", false, out.ocean, refs, issues);
  read_water(r, "lakes", true, out.lakes, refs, issues);
  read_water(r, "rivers", true, out.rivers, refs, issues);
  for_each_row(r, "region_sizes", issues, [&](ObjectReader& s) {
    RegionSize k;
    k.name = read_name(s, "name", issues);
    k.min_tiles = read_int(s, "min_tiles", 1, 66049);
    out.region_sizes.push_back(std::move(k));
  });

  for (std::size_t i = 0; i < refs.refs.size(); ++i) {
    const Biome* b = out.find(refs.refs[i].second);
    if (b == nullptr) {
      add_error(issues, refs.refs[i].first, "unknown biome \"" + refs.refs[i].second + "\"");
    } else {
      *refs.targets[i] = b->id;
    }
  }
}

const TemperatureBand* find_band(const BiomeTable& t, std::string_view name) noexcept {
  for (const TemperatureBand& b : t.temperature_bands) {
    if (b.name == name) {
      return &b;
    }
  }
  return nullptr;
}

std::optional<bool> base_is_wetland(const BiomeTable& t, std::string_view base) noexcept {
  for (const BaseBiomeRow& row : t.base) {
    if (row.base == base) {
      return row.wetland;
    }
  }
  return std::nullopt;
}

void check_biome_list(const BiomeTable& t, Issues& issues) {
  std::set<std::string> types;
  for (std::size_t i = 0; i < t.region_types.size(); ++i) {
    if (!types.insert(t.region_types[i]).second) {
      add_error(issues, io::json_index_path("$.region_types", i),
                "duplicate region type \"" + t.region_types[i] + "\"");
    }
  }
  check_unique_names(t.biomes, "$.biomes", issues);
  for (std::size_t i = 0; i < t.biomes.size(); ++i) {
    const std::string path = io::json_index_path("$.biomes", i);
    if (t.biomes[i].id != i) {
      add_error(issues, path + ".id",
                "is " + std::to_string(t.biomes[i].id) + ", expected " + std::to_string(i) +
                    " (ids equal the array position)");
    }
    if (types.count(t.biomes[i].region_type) == 0) {
      add_error(issues, path + ".region_type",
                "unknown region type \"" + t.biomes[i].region_type + "\"");
    }
  }
  // Every region type the REGION_COUNTS parameters address must exist.
  constexpr std::string_view prefix = "$.checks.REGION_COUNTS.";
  for (const ParamDescriptor& d : kParamDescriptors) {
    if (d.path.substr(0, prefix.size()) == prefix) {
      std::string type(d.path.substr(prefix.size()));
      std::transform(type.begin(), type.end(), type.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
      });
      if (types.count(type) == 0) {
        add_error(issues, "$.region_types",
                  "missing region type \"" + type + "\" of " + std::string(d.path));
      }
    }
  }
  if (t.thresholds.ocean_max_elevation >= t.thresholds.mountain_min_elevation) {
    add_error(issues, "$.thresholds.ocean_max_elevation", "must be below mountain_min_elevation");
  }
}

void check_bands(const BiomeTable& t, Issues& issues) {
  check_unique_names(t.temperature_bands, "$.temperature_bands", issues);
  std::int32_t next = -1000;
  for (std::size_t i = 0; i < t.temperature_bands.size(); ++i) {
    const TemperatureBand& b = t.temperature_bands[i];
    if (b.min != next) {
      add_error(issues, io::json_index_path("$.temperature_bands", i) + ".range",
                "starts at " + std::to_string(b.min) + ", expected " + std::to_string(next) +
                    " (bands must partition -1000..1000 in order)");
    }
    next = b.max + 1;
  }
  if (next != 1001) {
    add_error(issues, "$.temperature_bands", "the last band must end at 1000");
  }
}

// Every (rainfall, drainage) in 0..100 x 0..100 lies in exactly one base row.
void check_base_coverage(const BiomeTable& t, Issues& issues) {
  constexpr int kSide = 101;
  std::vector<int> owner(static_cast<std::size_t>(kSide) * kSide, -1);
  for (std::size_t i = 0; i < t.base.size(); ++i) {
    const BaseBiomeRow& row = t.base[i];
    bool reported = false;
    for (int p = row.rainfall_min; p <= row.rainfall_max; ++p) {
      for (int d = row.drainage_min; d <= row.drainage_max; ++d) {
        int& o = owner[static_cast<std::size_t>(p) * kSide + static_cast<std::size_t>(d)];
        if (o >= 0 && !reported) {
          add_error(issues, io::json_index_path("$.base", i),
                    "overlaps $.base[" + std::to_string(o) + "] at rainfall " + std::to_string(p) +
                        ", drainage " + std::to_string(d));
          reported = true;
        }
        if (o < 0) {
          o = static_cast<int>(i);
        }
      }
    }
    const std::optional<bool> wetland = base_is_wetland(t, row.base);
    if (wetland && *wetland != row.wetland) {
      add_error(issues, io::json_index_path("$.base", i) + ".wetland",
                "rows of base \"" + row.base + "\" disagree on wetland");
    }
  }
  for (int p = 0; p < kSide; ++p) {
    for (int d = 0; d < kSide; ++d) {
      if (owner[static_cast<std::size_t>(p) * kSide + static_cast<std::size_t>(d)] < 0) {
        add_error(issues, "$.base",
                  "rainfall " + std::to_string(p) + ", drainage " + std::to_string(d) +
                      " is covered by no row");
        return;
      }
    }
  }
}

// Each base label has one variant per non-frozen band (two for wetlands: fresh and saline).
void check_variants(const BiomeTable& t, Issues& issues) {
  std::set<std::pair<std::string, std::string>> fresh;
  std::set<std::pair<std::string, std::string>> saline;
  for (std::size_t i = 0; i < t.variants.size(); ++i) {
    const BiomeVariant& v = t.variants[i];
    const std::string path = io::json_index_path("$.variants", i);
    const std::optional<bool> wetland = base_is_wetland(t, v.base);
    if (!wetland) {
      add_error(issues, path + ".base", "unknown base \"" + v.base + "\"");
      continue;
    }
    if (*wetland != v.has_saline) {
      add_error(issues, path + ".saline",
                *wetland ? "wetland variants need saline" : "only wetland variants have saline");
      continue;
    }
    for (std::size_t j = 0; j < v.bands.size(); ++j) {
      const TemperatureBand* band = find_band(t, v.bands[j]);
      const std::string band_path = io::json_index_path(path + ".bands", j);
      if (band == nullptr || band->frozen) {
        add_error(issues, band_path,
                  band == nullptr ? "unknown band \"" + v.bands[j] + "\""
                                  : "frozen band \"" + v.bands[j] + "\" has no variants");
        continue;
      }
      auto& set = v.has_saline && v.saline ? saline : fresh;
      if (!set.emplace(v.base, v.bands[j]).second) {
        add_error(issues, band_path,
                  "second variant of base \"" + v.base + "\" in band \"" + v.bands[j] + "\"");
      }
    }
  }
  std::set<std::string> bases;
  for (const BaseBiomeRow& row : t.base) {
    if (!bases.insert(row.base).second) {
      continue;
    }
    for (const TemperatureBand& band : t.temperature_bands) {
      if (band.frozen) {
        continue;
      }
      const auto key = std::pair{row.base, band.name};
      if (fresh.count(key) == 0) {
        add_error(issues, "$.variants",
                  "no " + std::string(row.wetland ? "fresh " : "") + "variant of base \"" +
                      row.base + "\" in band \"" + band.name + "\"");
      }
      if (row.wetland && saline.count(key) == 0) {
        add_error(issues, "$.variants",
                  "no saline variant of base \"" + row.base + "\" in band \"" + band.name + "\"");
      }
    }
  }
  const std::optional<bool> mangrove_wetland = base_is_wetland(t, t.mangrove.base);
  if (!mangrove_wetland || !*mangrove_wetland) {
    add_error(issues, "$.mangrove.base", "\"" + t.mangrove.base + "\" is not a wetland base");
  }
  const TemperatureBand* mangrove_band = find_band(t, t.mangrove.band);
  if (mangrove_band == nullptr || mangrove_band->frozen) {
    add_error(issues, "$.mangrove.band",
              "\"" + t.mangrove.band + "\" is not a non-frozen temperature band");
  }
}

void check_water(const BiomeTable& t, std::string_view key, const std::vector<WaterBiome>& rows,
                 bool with_salinity, Issues& issues) {
  std::set<std::pair<std::string, std::int32_t>> seen;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const std::string path = io::json_index_path("$." + std::string(key), i);
    for (std::size_t j = 0; j < rows[i].bands.size(); ++j) {
      const std::string& band = rows[i].bands[j];
      if (find_band(t, band) == nullptr) {
        add_error(issues, io::json_index_path(path + ".bands", j), "unknown band \"" + band + "\"");
        continue;
      }
      const std::vector<std::int32_t> none = {-1};
      for (std::int32_t s : with_salinity ? rows[i].salinity : none) {
        if (!seen.emplace(band, s).second) {
          add_error(issues, io::json_index_path(path + ".bands", j),
                    "second row for band \"" + band + "\"" +
                        (with_salinity ? " and salinity " + std::to_string(s) : ""));
        }
      }
    }
  }
  if (!with_salinity) {
    for (const TemperatureBand& band : t.temperature_bands) {
      if (seen.count({band.name, -1}) == 0) {
        add_error(issues, "$." + std::string(key), "no row for band \"" + band.name + "\"");
      }
    }
  }
}

void check_biome_table(const BiomeTable& t, Issues& issues) {
  check_biome_list(t, issues);
  check_bands(t, issues);
  check_base_coverage(t, issues);
  check_variants(t, issues);
  check_water(t, "ocean", t.ocean, false, issues);
  check_water(t, "lakes", t.lakes, true, issues);
  check_water(t, "rivers", t.rivers, true, issues);
  check_unique_names(t.region_sizes, "$.region_sizes", issues);
  if (t.region_sizes.front().min_tiles != 1) {
    add_error(issues, "$.region_sizes[0].min_tiles", "the first size class must start at 1");
  }
  for (std::size_t i = 1; i < t.region_sizes.size(); ++i) {
    if (t.region_sizes[i].min_tiles <= t.region_sizes[i - 1].min_tiles) {
      add_error(issues, io::json_index_path("$.region_sizes", i) + ".min_tiles",
                "size classes must be sorted by increasing min_tiles");
    }
  }
}

// ---------------------------------------------------------------------------
// Geology
// ---------------------------------------------------------------------------

void read_rocks(ObjectReader& r, RockTable& out, Issues& issues) {
  for_each_row(r, "rocks", issues, [&](ObjectReader& k) {
    RockType rock;
    rock.name = read_name(k, "name", issues);
    rock.rock_class = read_enum(k, "class", rock_class_labels(), RockClass::sedimentary);
    rock.aquifer = read_flag(k, "aquifer");
    rock.shallow_ocean = read_flag(k, "shallow_ocean");
    rock.deep_ocean = read_flag(k, "deep_ocean");
    rock.erodibility = k.number("erodibility", 0.0, 0.01).value_or(0.0);
    if (rock.erodibility <= 0.0 && k.valid()) {
      add_error(issues, k.child("erodibility"), "must be positive");
    }
    rock.confidence = read_confidence(k);
    out.rocks.push_back(std::move(rock));
  });
}

void check_rocks(const RockTable& t, Issues& issues) {
  check_unique_names(t.rocks, "$.rocks", issues);
  const std::vector<std::string_view>& classes = rock_class_labels();
  for (std::size_t c = 0; c < classes.size(); ++c) {
    const bool present = std::any_of(t.rocks.begin(), t.rocks.end(), [&](const RockType& r) {
      return static_cast<std::size_t>(r.rock_class) == c;
    });
    if (!present) {
      add_error(issues, "$.rocks", "no rock of class " + std::string(classes[c]));
    }
  }
}

void read_soils(ObjectReader& r, SoilTable& out, Issues& issues) {
  for_each_row(r, "soils", issues, [&](ObjectReader& k) {
    SoilType soil;
    soil.name = read_name(k, "name", issues);
    soil.aquifer = read_flag(k, "aquifer");
    soil.permeability = k.number("permeability", 0.0, 1.0).value_or(0.0);
    soil.land = read_flag(k, "land");
    soil.shallow_ocean = read_flag(k, "shallow_ocean");
    soil.deep_ocean = read_flag(k, "deep_ocean");
    soil.sand_desert = read_flag(k, "sand_desert");
    soil.confidence = read_confidence(k);
    out.soils.push_back(std::move(soil));
  });
}

void check_soils(const SoilTable& t, Issues& issues) {
  check_unique_names(t.soils, "$.soils", issues);
  for (std::size_t i = 0; i < t.soils.size(); ++i) {
    const SoilType& s = t.soils[i];
    if (!s.land && !s.shallow_ocean && !s.deep_ocean) {
      add_error(issues, io::json_index_path("$.soils", i),
                "needs at least one of land, shallow_ocean, deep_ocean");
    }
  }
}

void read_minerals(ObjectReader& r, MineralTable& out, Issues& issues) {
  static const std::vector<std::string_view> kinds = {"ore", "fuel", "gem", "host"};
  static const std::vector<std::string_view> inclusions = {"vein", "cluster", "small_cluster",
                                                           "single"};
  for_each_row(r, "minerals", issues, [&](ObjectReader& k) {
    Mineral m;
    m.name = read_name(k, "name", issues);
    m.kind = read_enum(k, "kind", kinds, MineralKind::ore);
    for_each_row(k, "environments", issues, [&](ObjectReader& e) {
      MineralEnvironment env;
      int hosts = 0;
      for (const auto& [key, kind] :
           {std::pair{"host_class", HostKind::rock_class}, std::pair{"host_rock", HostKind::rock},
            std::pair{"host_mineral", HostKind::mineral}}) {
        const std::optional<std::string> host = e.string(key, false);
        if (!host) {
          continue;
        }
        ++hosts;
        env.host_kind = kind;
        env.host = *host;
        if (kind == HostKind::rock_class) {
          if (std::find(rock_class_labels().begin(), rock_class_labels().end(),
                        std::string_view(*host)) == rock_class_labels().end()) {
            add_error(issues, e.child(key), "\"" + *host + "\" is not a rock class");
          }
        }
      }
      if (hosts != 1) {
        add_error(issues, e.path(), "needs exactly one of host_class, host_rock, host_mineral");
      }
      env.inclusion = read_enum(e, "inclusion", inclusions, Inclusion::vein);
      env.frequency = read_int(e, "frequency", 1, 100);
      m.environments.push_back(std::move(env));
    });
    m.confidence = read_confidence(k);
    out.minerals.push_back(std::move(m));
  });
}

void check_minerals(const MineralTable& t, Issues& issues) {
  check_unique_names(t.minerals, "$.minerals", issues);
  for (std::size_t i = 0; i < t.minerals.size(); ++i) {
    const Mineral& m = t.minerals[i];
    for (std::size_t j = 0; j < m.environments.size(); ++j) {
      const MineralEnvironment& e = m.environments[j];
      if (e.host_kind != HostKind::mineral) {
        continue;
      }
      const std::string path =
          io::json_index_path(io::json_index_path("$.minerals", i) + ".environments", j) +
          ".host_mineral";
      const bool exists = std::any_of(t.minerals.begin(), t.minerals.end(),
                                      [&](const Mineral& o) { return o.name == e.host; });
      if (e.host == m.name) {
        add_error(issues, path, "a mineral cannot host itself");
      } else if (!exists) {
        add_error(issues, path, "unknown mineral \"" + e.host + "\"");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

void encode_names(detail::Encoder& e, const std::vector<std::string>& names) {
  e.u64(names.size());
  for (const std::string& n : names) {
    e.text(n);
  }
}

void encode_water(detail::Encoder& e, const std::vector<WaterBiome>& rows) {
  e.u64(rows.size());
  for (const WaterBiome& w : rows) {
    encode_names(e, w.bands);
    e.u64(w.salinity.size());
    for (std::int32_t s : w.salinity) {
      e.i64(s);
    }
    e.u64(w.biome);
    e.u8(static_cast<std::uint8_t>(w.confidence));
  }
}

}  // namespace

std::size_t ClimateClasses::classify(double temperature) const noexcept {
  std::size_t index = 0;
  for (std::size_t i = 0; i < classes.size(); ++i) {
    if (classes[i].min_value <= temperature) {
      index = i;
    }
  }
  return index;
}

std::int32_t SalinityClasses::quantise(double s) const noexcept {
  std::int32_t value = classes.empty() ? 0 : classes.front().value;
  for (const SalinityClass& c : classes) {
    if (c.min_salinity <= s) {
      value = c.value;
    }
  }
  return value;
}

const Biome* BiomeTable::find(std::string_view name) const noexcept {
  for (const Biome& b : biomes) {
    if (b.name == name) {
      return &b;
    }
  }
  return nullptr;
}

const RockType* RockTable::find(std::string_view name) const noexcept {
  for (const RockType& r : rocks) {
    if (r.name == name) {
      return &r;
    }
  }
  return nullptr;
}

core::Expected<BiomeTable> load_biome_table(std::string_view json) noexcept {
  return load_table<BiomeTable>(json, "biome_table", read_biomes, check_biome_table);
}

core::Expected<ClimateClasses> load_climate_classes(std::string_view json) noexcept {
  return load_table<ClimateClasses>(json, "climate_classes", read_climate, check_climate);
}

core::Expected<SalinityClasses> load_salinity_classes(std::string_view json) noexcept {
  return load_table<SalinityClasses>(json, "salinity_classes", read_salinity, check_salinity);
}

core::Expected<RockTable> load_rock_types(std::string_view json) noexcept {
  return load_table<RockTable>(json, "rock_types", read_rocks, check_rocks);
}

core::Expected<SoilTable> load_soils(std::string_view json) noexcept {
  return load_table<SoilTable>(json, "soils", read_soils, check_soils);
}

core::Expected<MineralTable> load_minerals(std::string_view json) noexcept {
  return load_table<MineralTable>(json, "minerals", read_minerals, check_minerals);
}

std::vector<ValidationIssue> validate_tables(const DataTables& tables) noexcept {
  Issues issues;
  const BiomeTable& b = tables.biomes;
  for (const auto& [key, rows] : {std::pair{"lakes", &b.lakes}, std::pair{"rivers", &b.rivers}}) {
    std::set<std::pair<std::string, std::int32_t>> covered;
    for (std::size_t i = 0; i < rows->size(); ++i) {
      for (std::size_t j = 0; j < (*rows)[i].salinity.size(); ++j) {
        const std::int32_t s = (*rows)[i].salinity[j];
        const bool is_class =
            std::any_of(tables.salinity.classes.begin(), tables.salinity.classes.end(),
                        [&](const SalinityClass& c) { return c.value == s; });
        if (!is_class) {
          add_error(issues,
                    "biome_table:" +
                        io::json_index_path(
                            io::json_index_path("$." + std::string(key), i) + ".salinity", j),
                    std::to_string(s) + " is not a salinity class value");
        }
        for (const std::string& band : (*rows)[i].bands) {
          covered.emplace(band, s);
        }
      }
    }
    for (const TemperatureBand& band : b.temperature_bands) {
      for (const SalinityClass& c : tables.salinity.classes) {
        if (covered.count({band.name, c.value}) == 0) {
          add_error(
              issues, "biome_table:$." + std::string(key),
              "no row for band \"" + band.name + "\" and salinity " + std::to_string(c.value));
        }
      }
    }
  }
  for (std::size_t i = 0; i < tables.minerals.minerals.size(); ++i) {
    const Mineral& m = tables.minerals.minerals[i];
    for (std::size_t j = 0; j < m.environments.size(); ++j) {
      const MineralEnvironment& e = m.environments[j];
      if (e.host_kind == HostKind::rock && tables.rocks.find(e.host) == nullptr) {
        add_error(
            issues,
            "minerals:" +
                io::json_index_path(io::json_index_path("$.minerals", i) + ".environments", j) +
                ".host_rock",
            "unknown rock \"" + e.host + "\"");
      }
    }
  }
  return issues;
}

core::Expected<DataTables> load_tables(const TableSources& sources) noexcept {
  DataTables tables;
  core::Status status;  // of the first table that fails
  auto take = [&status](auto expected, auto& target) {
    if (!status.ok()) {
      return;
    }
    if (!expected) {
      status = std::move(expected.error());
      return;
    }
    target = std::move(*expected);
  };
  take(load_biome_table(sources.biome_table), tables.biomes);
  take(load_climate_classes(sources.climate_classes), tables.climate);
  take(load_salinity_classes(sources.salinity_classes), tables.salinity);
  take(load_rock_types(sources.rock_types), tables.rocks);
  take(load_soils(sources.soils), tables.soils);
  take(load_minerals(sources.minerals), tables.minerals);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const Issues issues = validate_tables(tables);
  if (has_errors(issues)) {
    return core::make_error(core::StatusCode::invalid_argument, error_lines("tables", issues));
  }
  return tables;
}

core::Expected<DataTables> load_tables_dir(const std::filesystem::path& data_dir) noexcept {
  const std::array<std::filesystem::path, 6> files = {
      std::filesystem::path("tables") / "biome_table.json",
      std::filesystem::path("tables") / "climate_classes.json",
      std::filesystem::path("tables") / "salinity_classes.json",
      std::filesystem::path("geology") / "rock_types.json",
      std::filesystem::path("geology") / "soils.json",
      std::filesystem::path("geology") / "minerals.json",
  };
  std::array<std::string, 6> texts;
  for (std::size_t i = 0; i < files.size(); ++i) {
    core::Expected<std::string> text = io::read_text_file(data_dir / files[i]);
    if (!text) {
      return core::make_error(std::move(text.error()));
    }
    texts[i] = std::move(*text);
  }
  return load_tables(TableSources{texts[0], texts[1], texts[2], texts[3], texts[4], texts[5]});
}

std::uint64_t tables_hash(const DataTables& t) noexcept {
  detail::Encoder e;
  e.text("wg.tables");
  e.i64(kTablesSchemaVersion);

  const BiomeTable& b = t.biomes;
  encode_names(e, b.region_types);
  e.u64(b.biomes.size());
  for (const Biome& x : b.biomes) {
    e.u64(x.id);
    e.text(x.name);
    e.text(x.region_type);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  for (std::int32_t v : {b.thresholds.ocean_max_elevation, b.thresholds.mountain_min_elevation,
                         b.thresholds.glacier_min_drainage, b.thresholds.saline_min_salinity,
                         b.thresholds.mangrove_max_drainage}) {
    e.i64(v);
  }
  e.u64(b.temperature_bands.size());
  for (const TemperatureBand& x : b.temperature_bands) {
    e.text(x.name);
    e.i64(x.min);
    e.i64(x.max);
    e.u8(x.frozen ? 1 : 0);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(b.base.size());
  for (const BaseBiomeRow& x : b.base) {
    e.text(x.base);
    for (std::int32_t v : {x.rainfall_min, x.rainfall_max, x.drainage_min, x.drainage_max}) {
      e.i64(v);
    }
    e.u8(x.hills ? 1 : 0);
    e.u8(x.wetland ? 1 : 0);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(b.variants.size());
  for (const BiomeVariant& x : b.variants) {
    e.text(x.base);
    encode_names(e, x.bands);
    e.u8(x.has_saline ? 1 : 0);
    e.u8(x.saline ? 1 : 0);
    e.u64(x.biome);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.text(b.mangrove.base);
  e.text(b.mangrove.band);
  e.u64(b.mangrove.biome);
  e.u8(static_cast<std::uint8_t>(b.mangrove.confidence));
  e.u64(b.mountain);
  e.u64(b.tundra);
  e.u64(b.glacier);
  encode_water(e, b.ocean);
  encode_water(e, b.lakes);
  encode_water(e, b.rivers);
  e.u64(b.region_sizes.size());
  for (const RegionSize& x : b.region_sizes) {
    e.text(x.name);
    e.i64(x.min_tiles);
  }

  e.u64(t.climate.classes.size());
  for (const ClimateClass& x : t.climate.classes) {
    e.text(x.name);
    e.real(x.min_value);
    e.real(x.celsius_min);
    e.real(x.celsius_max);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(t.salinity.classes.size());
  for (const SalinityClass& x : t.salinity.classes) {
    e.text(x.name);
    e.i64(x.value);
    e.real(x.min_salinity);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(t.rocks.rocks.size());
  for (const RockType& x : t.rocks.rocks) {
    e.text(x.name);
    e.u8(static_cast<std::uint8_t>(x.rock_class));
    e.u8(x.aquifer ? 1 : 0);
    e.u8(x.shallow_ocean ? 1 : 0);
    e.u8(x.deep_ocean ? 1 : 0);
    e.real(x.erodibility);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(t.soils.soils.size());
  for (const SoilType& x : t.soils.soils) {
    e.text(x.name);
    e.u8(x.aquifer ? 1 : 0);
    e.real(x.permeability);
    e.u8(x.land ? 1 : 0);
    e.u8(x.shallow_ocean ? 1 : 0);
    e.u8(x.deep_ocean ? 1 : 0);
    e.u8(x.sand_desert ? 1 : 0);
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  e.u64(t.minerals.minerals.size());
  for (const Mineral& x : t.minerals.minerals) {
    e.text(x.name);
    e.u8(static_cast<std::uint8_t>(x.kind));
    e.u64(x.environments.size());
    for (const MineralEnvironment& env : x.environments) {
      e.u8(static_cast<std::uint8_t>(env.host_kind));
      e.text(env.host);
      e.u8(static_cast<std::uint8_t>(env.inclusion));
      e.i64(env.frequency);
    }
    e.u8(static_cast<std::uint8_t>(x.confidence));
  }
  return e.hash();
}

}  // namespace wg::params
