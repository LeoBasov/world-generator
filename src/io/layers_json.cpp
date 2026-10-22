#include "layers_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "params/reader.hpp"
#include "wg/params/validation.hpp"

namespace wg::io::detail {

namespace {

using params::ValidationIssue;
using params::detail::ObjectReader;

constexpr std::array<std::string_view, 2> kPlateTypes = {"continental", "oceanic"};
constexpr std::array<std::string_view, 3> kBoundaryClasses = {"convergent", "divergent",
                                                              "transform"};
constexpr std::array<std::string_view, 4> kInclusionTypes = {"vein", "cluster", "cluster_small",
                                                             "cluster_one"};

template <std::size_t N>
std::vector<std::string_view> label_list(const std::array<std::string_view, N>& labels) {
  return {labels.begin(), labels.end()};
}

template <std::size_t N>
std::size_t label_index(const std::string& label, const std::array<std::string_view, N>& labels) {
  for (std::size_t i = 0; i < N; ++i) {
    if (labels[i] == label) {
      return i;
    }
  }
  return 0;  // unreachable: ObjectReader::label checked the label
}

// --- Encoding --------------------------------------------------------------------------------

// Collects the first non-finite number; the location string is built only on failure.
class Encoder {
 public:
  double real(double v, std::string_view layer, std::size_t index, std::string_view member) {
    if (!std::isfinite(v) && status_.ok()) {
      status_ = core::make_status(core::StatusCode::invalid_argument,
                                  "layer " + std::string(layer) + "[" + std::to_string(index) +
                                      "]." + std::string(member) + " is not finite");
    }
    return v;
  }

  [[nodiscard]] const core::Status& status() const noexcept { return status_; }

 private:
  core::Status status_;
};

template <class T>
Json int_array(const std::vector<T>& values) {
  Json a = Json::array();
  for (const T v : values) {
    a.push_back(v);
  }
  return a;
}

Json encode_peaks(const std::vector<core::Peak>& peaks, Encoder& enc) {
  Json a = Json::array();
  for (std::size_t i = 0; i < peaks.size(); ++i) {
    const core::Peak& p = peaks[i];
    Json o = Json::object();
    o["cell"] = p.cell;
    o["height_eu"] = enc.real(p.height_eu, "peaks", i, "height_eu");
    o["radius_cells"] = enc.real(p.radius_cells, "peaks", i, "radius_cells");
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_volcanoes(const std::vector<core::Volcano>& volcanoes) {
  Json a = Json::array();
  for (const core::Volcano& v : volcanoes) {
    Json o = Json::object();
    o["cell"] = v.cell;
    o["magma_connected"] = v.magma_connected;
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_river_graph(const core::RiverGraph& g, Encoder& enc) {
  Json o = Json::object();
  o["cell"] = int_array(g.cell);
  o["downstream"] = int_array(g.downstream);
  Json q = Json::array();
  for (std::size_t i = 0; i < g.discharge_m3_yr.size(); ++i) {
    q.push_back(enc.real(g.discharge_m3_yr[i], "river_graph", i, "discharge_m3_yr"));
  }
  o["discharge_m3_yr"] = std::move(q);
  o["width_class"] = int_array(g.width_class);
  o["order"] = int_array(g.order);
  o["name_id"] = int_array(g.name_id);
  return o;
}

Json encode_lakes(const std::vector<core::Lake>& lakes, Encoder& enc) {
  Json a = Json::array();
  for (std::size_t i = 0; i < lakes.size(); ++i) {
    const core::Lake& l = lakes[i];
    Json o = Json::object();
    o["id"] = l.id;
    o["cells"] = int_array(l.cells);
    o["spill_elevation_eu"] = enc.real(l.spill_elevation_eu, "lakes", i, "spill_elevation_eu");
    o["outlet_cell"] = l.outlet_cell;
    o["salinity_class"] = l.salinity_class;
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_regions(const std::vector<core::Region>& regions, Encoder& enc) {
  Json a = Json::array();
  for (std::size_t i = 0; i < regions.size(); ++i) {
    const core::Region& r = regions[i];
    Json o = Json::object();
    o["id"] = r.id;
    o["type"] = r.type;
    o["area_cells"] = r.area_cells;
    o["centroid_x"] = enc.real(r.centroid_x, "regions", i, "centroid_x");
    o["centroid_y"] = enc.real(r.centroid_y, "regions", i, "centroid_y");
    o["biomes"] = int_array(r.biomes);
    o["size_class"] = r.size_class;
    o["name_id"] = r.name_id;
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_plates(const std::vector<core::Plate>& plates, Encoder& enc) {
  Json a = Json::array();
  for (std::size_t i = 0; i < plates.size(); ++i) {
    const core::Plate& p = plates[i];
    Json o = Json::object();
    o["id"] = p.id;
    o["type"] = kPlateTypes[static_cast<std::size_t>(p.type)];
    o["centroid_x"] = enc.real(p.centroid_x, "plates", i, "centroid_x");
    o["centroid_y"] = enc.real(p.centroid_y, "plates", i, "centroid_y");
    o["translation_x"] = enc.real(p.translation_x, "plates", i, "translation_x");
    o["translation_y"] = enc.real(p.translation_y, "plates", i, "translation_y");
    o["omega"] = enc.real(p.omega, "plates", i, "omega");
    Json boundaries = Json::array();
    for (const core::PlateBoundary& b : p.boundaries) {
      Json bo = Json::object();
      bo["other_plate"] = b.other_plate;
      bo["boundary_class"] = kBoundaryClasses[static_cast<std::size_t>(b.boundary_class)];
      bo["cells"] = int_array(b.cells);
      bo["convergence"] = enc.real(b.convergence, "plates", i, "boundaries.convergence");
      bo["shear"] = enc.real(b.shear, "plates", i, "boundaries.shear");
      boundaries.push_back(std::move(bo));
    }
    o["boundaries"] = std::move(boundaries);
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_geo_columns(const std::vector<core::GeoColumn>& columns, Encoder& enc) {
  Json a = Json::array();
  for (std::size_t i = 0; i < columns.size(); ++i) {
    const core::GeoColumn& c = columns[i];
    Json o = Json::object();
    o["geo_region"] = c.geo_region;
    Json layers = Json::array();
    for (const core::RockLayer& l : c.layers) {
      Json lo = Json::object();
      lo["rock_id"] = l.rock_id;
      lo["thickness_m"] = enc.real(l.thickness_m, "geo_columns", i, "layers.thickness_m");
      layers.push_back(std::move(lo));
    }
    o["layers"] = std::move(layers);
    Json soils = Json::array();
    for (const core::SoilLayer& l : c.soil_layers) {
      Json lo = Json::object();
      lo["soil_id"] = l.soil_id;
      lo["thickness_m"] = enc.real(l.thickness_m, "geo_columns", i, "soil_layers.thickness_m");
      soils.push_back(std::move(lo));
    }
    o["soil_layers"] = std::move(soils);
    o["cavern_levels"] = int_array(c.cavern_levels);
    a.push_back(std::move(o));
  }
  return a;
}

Json encode_minerals(const std::vector<core::MineralDeposit>& minerals) {
  Json a = Json::array();
  for (const core::MineralDeposit& m : minerals) {
    Json o = Json::object();
    o["mineral_id"] = m.mineral_id;
    o["host_layer"] = m.host_layer;
    o["inclusion"] = kInclusionTypes[static_cast<std::size_t>(m.inclusion)];
    o["cell"] = m.cell;
    a.push_back(std::move(o));
  }
  return a;
}

// --- Decoding --------------------------------------------------------------------------------

template <class T>
constexpr std::int64_t lowest_of() noexcept {
  return static_cast<std::int64_t>(std::numeric_limits<T>::lowest());
}

template <class T>
constexpr std::int64_t max_of() noexcept {
  if constexpr (sizeof(T) >= sizeof(std::int64_t)) {
    return std::int64_t{1} << 53;  // exact through ObjectReader's double
  } else {
    return static_cast<std::int64_t>(std::numeric_limits<T>::max());
  }
}

template <class T>
void get_int(ObjectReader& r, std::string_view key, T& out) {
  if (const auto v =
          r.integer(key, std::max(lowest_of<T>(), -(std::int64_t{1} << 53)), max_of<T>())) {
    out = static_cast<T>(*v);
  }
}

// Bounds of a value as doubles: the range of a float type, or of an integer type up to 2^53.
template <class T>
constexpr double low_bound() noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return static_cast<double>(std::numeric_limits<T>::lowest());
  } else {
    return static_cast<double>(std::max(lowest_of<T>(), -(std::int64_t{1} << 53)));
  }
}

template <class T>
constexpr double high_bound() noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return static_cast<double>(std::numeric_limits<T>::max());
  } else {
    return static_cast<double>(max_of<T>());
  }
}

template <class T>
void get_real(ObjectReader& r, std::string_view key, T& out) {
  if (const auto v = r.number(key, low_bound<T>(), high_bound<T>())) {
    out = static_cast<T>(*v);
  }
}

void get_bool(ObjectReader& r, std::string_view key, bool& out) {
  if (const auto v = r.boolean(key)) {
    out = *v;
  }
}

// Elements of a number array; builds a path only for a bad element, because river arrays can
// hold millions of values.
template <class T>
void get_array(ObjectReader& r, std::string_view key, std::vector<T>& out,
               std::vector<ValidationIssue>& issues) {
  const Json* a = r.array(key);
  if (a == nullptr) {
    return;
  }
  out.reserve(a->size());
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& e = (*a)[i];
    if constexpr (std::is_floating_point_v<T>) {
      if (e.is_number() && std::abs(e.get<double>()) <= high_bound<T>()) {
        out.push_back(static_cast<T>(e.get<double>()));
        continue;
      }
    } else if (e.is_number_integer()) {
      if (e.is_number_unsigned()) {
        const auto v = e.get<std::uint64_t>();
        if (v <= static_cast<std::uint64_t>(max_of<T>())) {
          out.push_back(static_cast<T>(v));
          continue;
        }
      } else {
        const auto v = e.get<std::int64_t>();
        if (v >= lowest_of<T>() && v <= max_of<T>()) {
          out.push_back(static_cast<T>(v));
          continue;
        }
      }
    }
    params::detail::read_number(e, json_index_path(r.child(key), i), low_bound<T>(),
                                high_bound<T>(), !std::is_floating_point_v<T>, issues);
    return;
  }
}

// Calls f(ObjectReader&) for every element of the array of objects under key.
template <class F>
void for_each_object(ObjectReader& r, std::string_view key, std::vector<ValidationIssue>& issues,
                     const F& f) {
  const Json* a = r.array(key);
  if (a == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < a->size(); ++i) {
    ObjectReader e((*a)[i], json_index_path(r.child(key), i), issues);
    if (e.valid()) {
      f(e);
      e.finish();
    }
  }
}

template <class E, std::size_t N>
void get_label(ObjectReader& r, std::string_view key, const std::array<std::string_view, N>& labels,
               E& out) {
  if (const auto s = r.label(key, label_list(labels))) {
    out = static_cast<E>(label_index(*s, labels));
  }
}

core::VectorLayers decode(ObjectReader& root, std::vector<ValidationIssue>& issues) {
  core::VectorLayers L;
  for_each_object(root, "peaks", issues, [&](ObjectReader& r) {
    core::Peak& p = L.peaks.emplace_back();
    get_int(r, "cell", p.cell);
    get_real(r, "height_eu", p.height_eu);
    get_real(r, "radius_cells", p.radius_cells);
  });
  for_each_object(root, "volcanoes", issues, [&](ObjectReader& r) {
    core::Volcano& v = L.volcanoes.emplace_back();
    get_int(r, "cell", v.cell);
    get_bool(r, "magma_connected", v.magma_connected);
  });
  if (const Json* g = root.object("river_graph"); g != nullptr) {
    ObjectReader r(*g, root.child("river_graph"), issues);
    core::RiverGraph& rg = L.river_graph;
    get_array(r, "cell", rg.cell, issues);
    get_array(r, "downstream", rg.downstream, issues);
    get_array(r, "discharge_m3_yr", rg.discharge_m3_yr, issues);
    get_array(r, "width_class", rg.width_class, issues);
    get_array(r, "order", rg.order, issues);
    get_array(r, "name_id", rg.name_id, issues);
    r.finish();
    if (!params::has_errors(issues) && !rg.consistent()) {
      params::detail::add_error(issues, r.path(), "arrays differ in length");
    }
  }
  for_each_object(root, "lakes", issues, [&](ObjectReader& r) {
    core::Lake& l = L.lakes.emplace_back();
    get_int(r, "id", l.id);
    get_array(r, "cells", l.cells, issues);
    get_real(r, "spill_elevation_eu", l.spill_elevation_eu);
    get_int(r, "outlet_cell", l.outlet_cell);
    get_int(r, "salinity_class", l.salinity_class);
  });
  for_each_object(root, "regions", issues, [&](ObjectReader& r) {
    core::Region& g = L.regions.emplace_back();
    get_int(r, "id", g.id);
    get_int(r, "type", g.type);
    get_int(r, "area_cells", g.area_cells);
    get_real(r, "centroid_x", g.centroid_x);
    get_real(r, "centroid_y", g.centroid_y);
    get_array(r, "biomes", g.biomes, issues);
    get_int(r, "size_class", g.size_class);
    get_int(r, "name_id", g.name_id);
  });
  for_each_object(root, "plates", issues, [&](ObjectReader& r) {
    core::Plate& p = L.plates.emplace_back();
    get_int(r, "id", p.id);
    get_label(r, "type", kPlateTypes, p.type);
    get_real(r, "centroid_x", p.centroid_x);
    get_real(r, "centroid_y", p.centroid_y);
    get_real(r, "translation_x", p.translation_x);
    get_real(r, "translation_y", p.translation_y);
    get_real(r, "omega", p.omega);
    for_each_object(r, "boundaries", issues, [&](ObjectReader& b) {
      core::PlateBoundary& pb = p.boundaries.emplace_back();
      get_int(b, "other_plate", pb.other_plate);
      get_label(b, "boundary_class", kBoundaryClasses, pb.boundary_class);
      get_array(b, "cells", pb.cells, issues);
      get_real(b, "convergence", pb.convergence);
      get_real(b, "shear", pb.shear);
    });
  });
  for_each_object(root, "geo_columns", issues, [&](ObjectReader& r) {
    core::GeoColumn& c = L.geo_columns.emplace_back();
    get_int(r, "geo_region", c.geo_region);
    for_each_object(r, "layers", issues, [&](ObjectReader& l) {
      core::RockLayer& rl = c.layers.emplace_back();
      get_int(l, "rock_id", rl.rock_id);
      get_real(l, "thickness_m", rl.thickness_m);
    });
    for_each_object(r, "soil_layers", issues, [&](ObjectReader& l) {
      core::SoilLayer& sl = c.soil_layers.emplace_back();
      get_int(l, "soil_id", sl.soil_id);
      get_real(l, "thickness_m", sl.thickness_m);
    });
    get_array(r, "cavern_levels", c.cavern_levels, issues);
  });
  for_each_object(root, "minerals", issues, [&](ObjectReader& r) {
    core::MineralDeposit& m = L.minerals.emplace_back();
    get_int(r, "mineral_id", m.mineral_id);
    get_int(r, "host_layer", m.host_layer);
    get_label(r, "inclusion", kInclusionTypes, m.inclusion);
    get_int(r, "cell", m.cell);
  });
  return L;
}

}  // namespace

core::Expected<Json> encode_layers(const core::VectorLayers& layers) noexcept {
  try {
    if (!layers.river_graph.consistent()) {
      return core::make_error(core::StatusCode::invalid_argument,
                              "layer river_graph: arrays differ in length");
    }
    Encoder enc;
    Json o = Json::object();
    o["peaks"] = encode_peaks(layers.peaks, enc);
    o["volcanoes"] = encode_volcanoes(layers.volcanoes);
    o["river_graph"] = encode_river_graph(layers.river_graph, enc);
    o["lakes"] = encode_lakes(layers.lakes, enc);
    o["regions"] = encode_regions(layers.regions, enc);
    o["plates"] = encode_plates(layers.plates, enc);
    o["geo_columns"] = encode_geo_columns(layers.geo_columns, enc);
    o["minerals"] = encode_minerals(layers.minerals);
    if (!enc.status().ok()) {
      return core::make_error(enc.status());
    }
    return o;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::out_of_memory,
                            std::string("encoding vector layers failed: ") + e.what());
  }
}

core::Expected<core::VectorLayers> decode_layers(const Json& json, std::string_view path) noexcept {
  try {
    std::vector<ValidationIssue> issues;
    ObjectReader root(json, std::string(path), issues);
    core::VectorLayers layers;
    if (root.valid()) {
      layers = decode(root, issues);
      root.finish();
    }
    if (params::has_errors(issues)) {
      std::string text = "invalid vector layers:";
      for (const ValidationIssue& i : issues) {
        text += "\n" + i.path + ": " + i.message;
      }
      return core::make_error(core::StatusCode::parse_error, text);
    }
    return layers;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::out_of_memory,
                            std::string("decoding vector layers failed: ") + e.what());
  }
}

}  // namespace wg::io::detail
