// wgcli inspect: print header, provenance and field statistics (plan section 6.7).

#include <algorithm>
#include <array>
#include <iomanip>
#include <string>
#include <vector>

#include "commands.hpp"
#include "field_values.hpp"
#include "wg/core/executor.hpp"
#include "wg/io/world_file.hpp"
#include "wg/numerics/histogram.hpp"
#include "world_hash.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"field", "NAME", "Field to report (default: every field)", OptionKind::list},
    OptionSpec{"histogram", "BINS", "Also print a histogram with BINS bins per field",
               OptionKind::value},
};

std::string_view codec_name(io::Codec codec) noexcept {
  return codec == io::Codec::zstd ? "zstd" : "raw";
}

std::string dtype_name(std::uint8_t dtype) {
  switch (dtype) {
    case static_cast<std::uint8_t>(core::DType::u8):
      return "u8";
    case static_cast<std::uint8_t>(core::DType::u16):
      return "u16";
    case static_cast<std::uint8_t>(core::DType::i32):
      return "i32";
    case static_cast<std::uint8_t>(core::DType::f32):
      return "f32";
    case io::kMetadataDType:
      return "json";
    default:
      return std::to_string(dtype);
  }
}

void print_header(std::ostream& out, const io::WorldReader& reader) {
  const io::WorldHeader& h = reader.header();
  const core::Provenance& p = reader.provenance();
  out << "format version: " << h.format_version << "\n"
      << "library version: " << h.library_version.major << "." << h.library_version.minor << "."
      << h.library_version.patch << "\n"
      << "grid: n=" << h.grid.n << " dx_m=" << h.grid.dx_m << " hu_m=" << h.grid.hu_m << "\n"
      << "seed: " << h.seed << "\n"
      << "params hash: " << hex64(h.params_hash) << "\n"
      << "profile: " << p.profile << "\n"
      << "attempt: " << p.attempt << "\n"
      << "completed stages:";
  for (const std::string& s : p.completed_stages) {
    out << " " << s;
  }
  out << "\nrejections: " << p.rejections.size() << "\n";
  print_rejections(out, p);
  out << "chunks: " << reader.chunks().size() << "\n";
  for (const io::ChunkEntry& c : reader.chunks()) {
    const double ratio = c.raw_size == 0
                             ? 1.0
                             : static_cast<double>(c.stored_size) / static_cast<double>(c.raw_size);
    out << "  " << std::left << std::setw(20) << c.name << std::right << " " << std::setw(4)
        << dtype_name(c.dtype) << " " << std::setw(4) << codec_name(c.codec) << " raw "
        << c.raw_size << " stored " << c.stored_size << " (" << std::fixed << std::setprecision(3)
        << ratio << ")" << std::defaultfloat << std::setprecision(6) << "\n";
  }
  for (const std::string& name : reader.unknown_chunks()) {
    out << "unknown chunk (skipped): " << name << "\n";
  }
  const core::VectorLayers& L = reader.layers();
  out << "layers: peaks=" << L.peaks.size() << " volcanoes=" << L.volcanoes.size()
      << " river_nodes=" << L.river_graph.cell.size() << " lakes=" << L.lakes.size()
      << " regions=" << L.regions.size() << " plates=" << L.plates.size()
      << " geo_columns=" << L.geo_columns.size() << " minerals=" << L.minerals.size() << "\n";
}

core::Status print_histogram(std::ostream& out, const std::vector<double>& values,
                             const FieldStats& stats, std::int32_t bins) {
  std::vector<float> data(values.size());
  std::transform(values.begin(), values.end(), data.begin(),
                 [](double v) { return static_cast<float>(v); });
  const double hi = stats.max > stats.min ? stats.max : stats.min + 1.0;
  core::SerialExecutor exec;
  auto h =
      num::histogram(data, {}, num::HistogramParams{.lo = stats.min, .hi = hi, .bins = bins}, exec);
  if (!h) {
    return std::move(h.error());
  }
  const double width = (h->hi - h->lo) / bins;
  for (std::int32_t b = 0; b < bins; ++b) {
    out << "    [" << h->lo + (b * width) << ", " << h->lo + ((b + 1) * width) << ") "
        << h->counts[static_cast<std::size_t>(b)] << "\n";
  }
  return {};
}

}  // namespace

const CommandSpec kInspectSpec{
    .name = "inspect",
    .summary = "Print header, provenance and field statistics of a world",
    .positionals = "WORLD",
    .min_positionals = 1,
    .max_positionals = 1,
    .description =
        "Reads the header, chunk table and metadata of WORLD, then only the chunks of the\n"
        "requested fields, and prints min, max, mean and the count of non-finite values.",
    .options = kOptions,
};

int run_inspect(const ParsedArgs& args, const Io& io) {
  std::int32_t bins = 0;
  if (auto text = args.value("histogram")) {
    auto parsed = parse_int("histogram", *text);
    if (!parsed) {
      return fail(io, parsed.error());
    }
    if (*parsed < 1 || *parsed > 1000) {
      return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                        "option --histogram: BINS must be in [1, 1000]"));
    }
    bins = static_cast<std::int32_t>(*parsed);
  }
  const std::string& path = args.positionals()[0];
  auto reader = io::WorldReader::open_file(path);
  if (!reader) {
    return fail(io, core::make_status(reader.error().code, path + ": " + reader.error().message));
  }
  auto fields = select_fields(args.values("field"), *reader);
  if (!fields) {
    return fail(io, fields.error());
  }
  print_header(io.out, *reader);
  io.out << "fields: " << fields->size() << "\n";
  for (const core::FieldId id : *fields) {
    auto values = read_values(*reader, id);
    if (!values) {
      return fail(io, core::make_status(values.error().code, path + ": " + values.error().message));
    }
    const FieldStats s = field_stats(*values);
    io.out << "  " << core::field_info(id).name << ": min=" << s.min << " max=" << s.max
           << " mean=" << s.mean << " non_finite=" << s.non_finite << "\n";
    if (bins > 0) {
      if (core::Status st = print_histogram(io.out, *values, s, bins); !st.ok()) {
        return fail(io, st);
      }
    }
  }
  return 0;
}

}  // namespace wg::cli
