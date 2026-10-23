// wgcli compare: field-wise difference of two worlds (plan section 6.7).

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "commands.hpp"
#include "field_values.hpp"
#include "image.hpp"
#include "wg/io/export.hpp"
#include "wg/io/world_file.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"field", "NAME", "Field to compare (default: every field)", OptionKind::list},
    OptionSpec{"tolerance", "T", "Largest accepted absolute difference (default 0)",
               OptionKind::value},
    OptionSpec{"diff-png", "DIR", "Write DIR/<field>_diff.png of |A - B| per differing field",
               OptionKind::value},
};

struct FieldDiff {
  double max_abs = 0.0;
  std::int64_t over = 0;  ///< cells with |a - b| > tolerance
  std::vector<double> abs;
};

FieldDiff diff(const std::vector<double>& a, const std::vector<double>& b, double tolerance) {
  FieldDiff d;
  d.abs.resize(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    double v = 0.0;  // equal values, equal infinities and two NaNs do not differ
    if (!(std::isnan(a[i]) && std::isnan(b[i])) && a[i] != b[i]) {
      v = std::abs(a[i] - b[i]);
      if (std::isnan(v)) {
        v = std::numeric_limits<double>::infinity();
      }
    }
    d.abs[i] = v;
    d.max_abs = std::max(d.max_abs, v);
    if (v > tolerance) {
      ++d.over;
    }
  }
  return d;
}

core::Status write_diff_png(const std::filesystem::path& path, std::int32_t n, const FieldDiff& d) {
  // Scale to the largest finite difference; infinite ones are white.
  double hi = 0.0;
  for (const double v : d.abs) {
    if (std::isfinite(v)) {
      hi = std::max(hi, v);
    }
  }
  std::vector<std::uint16_t> grey(d.abs.size());
  for (std::size_t i = 0; i < d.abs.size(); ++i) {
    grey[i] = std::isfinite(d.abs[i]) ? io::png16_value(d.abs[i], 0.0, hi) : 65535;
  }
  return write_grey16_png(path, n, grey);
}

}  // namespace

const CommandSpec kCompareSpec{
    .name = "compare",
    .summary = "Field-wise difference of two worlds",
    .positionals = "A B",
    .min_positionals = 2,
    .max_positionals = 2,
    .description =
        "Compares the fields of two worlds on the same grid and their vector layers. Prints\n"
        "the largest absolute difference and the number of cells above the tolerance per\n"
        "field. Exits with 0 if the worlds agree within the tolerance, 1 if they differ.",
    .options = kOptions,
};

int run_compare(const ParsedArgs& args, const Io& io) {
  double tolerance = 0.0;
  if (auto text = args.value("tolerance")) {
    auto parsed = parse_double("tolerance", *text);
    if (!parsed) {
      return fail(io, parsed.error());
    }
    if (*parsed < 0.0) {
      return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                        "option --tolerance: must not be negative"));
    }
    tolerance = *parsed;
  }
  const std::string& path_a = args.positionals()[0];
  const std::string& path_b = args.positionals()[1];
  auto a = io::WorldReader::open_file(path_a);
  if (!a) {
    return fail(io, core::make_status(a.error().code, path_a + ": " + a.error().message));
  }
  auto b = io::WorldReader::open_file(path_b);
  if (!b) {
    return fail(io, core::make_status(b.error().code, path_b + ": " + b.error().message));
  }
  if (!(a->grid() == b->grid())) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "the worlds have different grids"));
  }

  // Fields of either world, or the requested ones.
  std::vector<core::FieldId> ids;
  const std::vector<std::string> names = args.values("field");
  if (names.empty() || std::find(names.begin(), names.end(), "all") != names.end()) {
    for (std::size_t i = 0; i < core::kFieldCount; ++i) {
      const auto id = static_cast<core::FieldId>(i);
      if (a->has(id) || b->has(id)) {
        ids.push_back(id);
      }
    }
  } else {
    for (const std::string& name : names) {
      const std::optional<core::FieldId> id = core::field_id_from_name(name);
      if (!id) {
        return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                          "unknown field '" + name + "'"));
      }
      ids.push_back(*id);
    }
  }

  const std::optional<std::string> png_dir = args.value("diff-png");
  bool differ = false;
  for (const core::FieldId id : ids) {
    const std::string name(core::field_info(id).name);
    if (!a->has(id) || !b->has(id)) {
      io.out << name << ": only in " << (a->has(id) ? "A" : "B") << "\n";
      differ = true;
      continue;
    }
    auto va = read_values(*a, id);
    if (!va) {
      return fail(io, core::make_status(va.error().code, path_a + ": " + va.error().message));
    }
    auto vb = read_values(*b, id);
    if (!vb) {
      return fail(io, core::make_status(vb.error().code, path_b + ": " + vb.error().message));
    }
    const FieldDiff d = diff(*va, *vb, tolerance);
    io.out << name << ": max_abs_diff=" << d.max_abs << " cells_over_tolerance=" << d.over << "\n";
    if (d.over > 0) {
      differ = true;
    }
    if (png_dir && d.max_abs > 0.0) {
      const std::filesystem::path path = std::filesystem::path(*png_dir) / (name + "_diff.png");
      if (core::Status s = write_diff_png(path, a->grid().n, d); !s.ok()) {
        return fail(io, s);
      }
    }
  }

  auto layers_a = io::layers_to_json(a->layers());
  auto layers_b = io::layers_to_json(b->layers());
  if (!layers_a || !layers_b) {
    return fail(io, layers_a ? layers_b.error() : layers_a.error());
  }
  const bool layers_equal = *layers_a == *layers_b;
  io.out << "layers: " << (layers_equal ? "equal" : "differ") << "\n";
  differ = differ || !layers_equal;
  io.out << (differ ? "result: differ" : "result: equal") << "\n";
  return static_cast<int>(differ ? ExitCode::generation_failed : ExitCode::ok);
}

}  // namespace wg::cli
