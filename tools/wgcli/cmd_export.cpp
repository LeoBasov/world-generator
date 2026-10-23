// wgcli export: PNG16, EXR and JSON vector layers (plan sections 6.5 and 6.7).

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "commands.hpp"
#include "field_values.hpp"
#include "image.hpp"
#include "wg/io/export.hpp"
#include "wg/io/world_file.hpp"
#include "wg/pipeline/field_set.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"format", "png16|exr|json", "Output format, required", OptionKind::value},
    OptionSpec{"field", "NAME|all", "Field to export (default all; not for json)",
               OptionKind::list},
    OptionSpec{"colormap", "grey|terrain|viridis",
               "Also write <field>_<map>.png for review (png16 only)", OptionKind::value},
    OptionSpec{"out", "DIR", "Output directory, required", OptionKind::value},
};

}  // namespace

const CommandSpec kExportSpec{
    .name = "export",
    .summary = "Export fields as PNG16 or EXR, vector layers as JSON",
    .positionals = "WORLD",
    .min_positionals = 1,
    .max_positionals = 1,
    .description =
        "png16 writes DIR/<field>.png scaled by the field's registry range (or its data range)\n"
        "and stores the range in the PNG. exr writes DIR/<field>.exr with the values unscaled\n"
        "(only if the library was built with WG_WITH_EXR). json writes DIR/layers.json.\n"
        "Colour maps are for looking at, never for reading back.",
    .options = kOptions,
};

int run_export(const ParsedArgs& args, const Io& io) {
  const std::optional<std::string> format = args.value("format");
  const std::optional<std::string> out = args.value("out");
  if (!format || !out) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "export: options --format and --out are required"));
  }
  if (*format != "png16" && *format != "exr" && *format != "json") {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "option --format: expected png16, exr or json"));
  }
  std::optional<Colormap> colormap;
  if (auto name = args.value("colormap")) {
    colormap = colormap_from_name(*name);
    if (!colormap || *format != "png16") {
      return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                        "option --colormap: expected grey, terrain or viridis "
                                        "with --format png16"));
    }
  }
  if (*format == "exr" && !io::exr_supported()) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "EXR export is not available: the library was built "
                                      "without WG_WITH_EXR"));
  }

  const std::string& path = args.positionals()[0];
  auto reader = io::WorldReader::open_file(path);
  if (!reader) {
    return fail(io, core::make_status(reader.error().code, path + ": " + reader.error().message));
  }
  const std::filesystem::path dir(*out);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    return fail(io, core::make_status(core::StatusCode::io_error,
                                      "cannot create " + dir.string() + ": " + ec.message()));
  }

  if (*format == "json") {
    const std::filesystem::path file = dir / "layers.json";
    if (core::Status s = io::export_layers_json(file, reader->layers()); !s.ok()) {
      return fail(io, s);
    }
    io.out << file.string() << "\n";
    return 0;
  }

  auto ids = select_fields(args.values("field"), *reader);
  if (!ids) {
    return fail(io, ids.error());
  }
  pipeline::FieldSet wanted;
  for (const core::FieldId id : *ids) {
    wanted.add(id);
  }
  auto world = io::read_world_file(path, io::ReadOptions{.fields = &wanted});
  if (!world) {
    return fail(io, core::make_status(world.error().code, path + ": " + world.error().message));
  }
  for (const core::FieldId id : *ids) {
    const std::string name(core::field_info(id).name);
    if (*format == "exr") {
      const std::filesystem::path file = dir / (name + ".exr");
      if (core::Status s = io::export_exr(file, world->state, id); !s.ok()) {
        return fail(io, s);
      }
      io.out << file.string() << "\n";
      continue;
    }
    auto png = io::encode_png16(world->state, id);
    if (!png) {
      return fail(io, png.error());
    }
    const std::filesystem::path file = dir / (name + ".png");
    const std::string_view bytes(reinterpret_cast<const char*>(png->png.data()), png->png.size());
    if (core::Status s = write_file(file, bytes); !s.ok()) {
      return fail(io, s);
    }
    io.out << file.string() << " range [" << png->lo << ", " << png->hi << "]\n";
    if (colormap) {
      auto values = read_values(*reader, id);
      if (!values) {
        return fail(io, values.error());
      }
      const std::filesystem::path map_file =
          dir / (name + "_" + std::string(*args.value("colormap")) + ".png");
      if (core::Status s =
              write_colormap_png(map_file, reader->grid().n, *values, png->lo, png->hi, *colormap);
          !s.ok()) {
        return fail(io, s);
      }
      io.out << map_file.string() << "\n";
    }
  }
  return 0;
}

}  // namespace wg::cli
