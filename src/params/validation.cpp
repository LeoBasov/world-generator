#include "wg/params/validation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reader.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/params/params.hpp"

namespace wg::params {

namespace {

using detail::add_error;
using detail::add_warning;
using detail::format_number;

bool is_power_of_two(std::int64_t v) noexcept { return v > 0 && (v & (v - 1)) == 0; }

void check_bounds(const GenParams& p, std::vector<ValidationIssue>& issues) {
  for (const ParamDescriptor& d : kParamDescriptors) {
    const std::size_t n = d.count == 0 ? 1 : d.count;
    for (std::size_t i = 0; i < n; ++i) {
      const double v = get_param(p, d, i);
      const std::string path = d.count == 0 ? std::string(d.path) : io::json_index_path(d.path, i);
      if (std::isnan(v)) {
        add_error(issues, path, "is not a number");
      } else if (v < d.min) {
        add_error(issues, path, format_number(v) + " is below the minimum " + format_number(d.min));
      } else if (v > d.max) {
        add_error(issues, path, format_number(v) + " is above the maximum " + format_number(d.max));
      }
    }
  }
}

struct FieldEntry {
  std::string_view name;
  const FieldGenParams& gen;
};

void check_fields(const GenParams& p, std::vector<ValidationIssue>& issues) {
  const std::array<FieldEntry, 6> fields = {{
      {"elevation", p.fields.elevation},
      {"rainfall", p.fields.rainfall},
      {"temperature", p.fields.temperature},
      {"drainage", p.fields.drainage},
      {"volcanism", p.fields.volcanism},
      {"savagery", p.fields.savagery},
  }};
  for (const FieldEntry& f : fields) {
    const std::string base = "$.fields." + std::string(f.name);
    if (f.gen.min > f.gen.max) {
      add_error(issues, base + ".min",
                "min " + format_number(f.gen.min) + " is above max " + format_number(f.gen.max));
    }
    const std::optional<core::FieldId> id = core::field_id_from_name(f.name);
    if (id) {
      const core::FieldRange& range = core::field_info(*id).range;
      for (const auto& [key, v] : {std::pair{"min", f.gen.min}, std::pair{"max", f.gen.max}}) {
        if (range.has_min && v < range.min) {
          add_error(issues, base + "." + key,
                    format_number(v) + " is below the minimum " + format_number(range.min) +
                        " of field " + std::string(f.name));
        }
        if (range.has_max && v > range.max) {
          add_error(issues, base + "." + key,
                    format_number(v) + " is above the maximum " + format_number(range.max) +
                        " of field " + std::string(f.name));
        }
      }
    }
    double weight_sum = 0.0;
    for (double w : f.gen.weights) {
      weight_sum += w;
    }
    if (!(weight_sum > 0.0)) {
      add_error(issues, base + ".weights", "at least one weight must be positive");
    }
  }
}

void check_pairs(const GenParams& p, std::vector<ValidationIssue>& issues) {
  if (!is_power_of_two(p.grid.size - 1)) {
    add_error(issues, "$.grid.size",
              std::to_string(p.grid.size) + " is not 2^n + 1 (17, 33, 65, 129 or 257)");
  }
  if (!is_power_of_two(p.grid.refinement)) {
    add_error(issues, "$.grid.refinement",
              std::to_string(p.grid.refinement) + " is not one of 1, 2, 4, 8");
  }
  const ElevationPostParams& e = p.elevation_post;
  if (!(e.plains_z_lo < e.plains_z_hi)) {
    add_error(issues, "$.elevation_post.plains_z_lo",
              "plains band lower edge " + format_number(e.plains_z_lo) +
                  " is not below the upper edge " + format_number(e.plains_z_hi));
  }
  const GeologyParams& g = p.geology;
  if (g.layer_count_min > g.layer_count_max) {
    add_error(issues, "$.geology.layer_count_min",
              "layer_count_min " + std::to_string(g.layer_count_min) +
                  " is above layer_count_max " + std::to_string(g.layer_count_max));
  }
  struct Pair {
    std::string_view name;
    const std::array<std::int32_t, 3>& lo;
    const std::array<std::int32_t, 3>& hi;
  };
  const std::array<Pair, 3> caverns = {{
      {"CAVERN_LAYER_OPENNESS", g.cavern_layer_openness_min, g.cavern_layer_openness_max},
      {"CAVERN_LAYER_PASSAGE_DENSITY", g.cavern_layer_passage_density_min,
       g.cavern_layer_passage_density_max},
      {"CAVERN_LAYER_WATER", g.cavern_layer_water_min, g.cavern_layer_water_max},
  }};
  for (const Pair& c : caverns) {
    for (std::size_t i = 0; i < 3; ++i) {
      if (c.lo[i] > c.hi[i]) {
        add_error(issues, io::json_index_path("$.geology." + std::string(c.name) + "_MIN", i),
                  "min " + std::to_string(c.lo[i]) + " is above max " + std::to_string(c.hi[i]));
      }
    }
  }
}

void check_counts(const GenParams& p, std::vector<ValidationIssue>& issues) {
  const std::int64_t cells = static_cast<std::int64_t>(p.grid.size) * p.grid.size;
  struct Ranges {
    std::string_view name;
    const std::array<std::int32_t, 3>& mins;
  };
  const ChecksParams& c = p.checks;
  const std::array<Ranges, 5> ranges = {{
      {"ELEVATION_RANGES", c.elevation_ranges},
      {"RAIN_RANGES", c.rain_ranges},
      {"DRAINAGE_RANGES", c.drainage_ranges},
      {"VOLCANISM_RANGES", c.volcanism_ranges},
      {"SAVAGERY_RANGES", c.savagery_ranges},
  }};
  for (const Ranges& r : ranges) {
    const std::int64_t sum = std::int64_t{r.mins[0]} + r.mins[1] + r.mins[2];
    if (sum > cells) {
      add_error(issues, "$.checks." + std::string(r.name),
                "the low, mid and high minimums sum to " + std::to_string(sum) +
                    " squares, more than the " + std::to_string(cells) + " of the grid");
    }
  }
}

// Contradictions of the report: a check that no world can pass with these field ranges.
void check_contradictions(const GenParams& p, std::vector<ValidationIssue>& issues) {
  const double z_max = p.fields.elevation.max;
  if (p.checks.peak_number_min > 0 && z_max < 400.0) {
    add_warning(issues, "$.checks.PEAK_NUMBER_MIN",
                "peaks need elevation 400 but $.fields.elevation.max is " + format_number(z_max) +
                    "; every attempt will be rejected (R4.3)");
  }
  if (p.checks.volcano_min > 0 && p.fields.volcanism.max < 100.0) {
    add_warning(issues, "$.checks.VOLCANO_MIN",
                "volcanoes need volcanism 100 but $.fields.volcanism.max is " +
                    format_number(p.fields.volcanism.max) +
                    "; every attempt will be rejected (R4.5)");
  }
  if ((p.checks.river_mins[0] > 0 || p.checks.river_mins[1] > 0) && z_max < 104.0) {
    add_warning(issues, "$.checks.RIVER_MINS",
                "rivers need a maximum elevation of at least 104 but $.fields.elevation.max is " +
                    format_number(z_max) + "; every attempt will be rejected (R4.2)");
  }
  const double z_min = p.fields.elevation.min;
  if ((p.checks.partial_ocean_edge_min > 0 || p.checks.complete_ocean_edge_min > 0) &&
      z_min >= 100.0) {
    add_warning(issues, "$.checks.PARTIAL_OCEAN_EDGE_MIN",
                "edge oceans need elevation below 100 but $.fields.elevation.min is " +
                    format_number(z_min) + "; every attempt will be rejected (R4.2)");
  }
}

}  // namespace

std::vector<ValidationIssue> validate(const GenParams& params) noexcept {
  std::vector<ValidationIssue> issues;
  check_bounds(params, issues);
  check_fields(params, issues);
  check_pairs(params, issues);
  check_counts(params, issues);
  check_contradictions(params, issues);
  return issues;
}

bool has_errors(std::span<const ValidationIssue> issues) noexcept {
  for (const ValidationIssue& i : issues) {
    if (i.severity == Severity::error) {
      return true;
    }
  }
  return false;
}

std::string format_issues(std::span<const ValidationIssue> issues) noexcept {
  std::string text;
  for (const ValidationIssue& i : issues) {
    text += i.severity == Severity::error ? "error: " : "warning: ";
    text += i.path;
    text += ": ";
    text += i.message;
    text += '\n';
  }
  return text;
}

}  // namespace wg::params
