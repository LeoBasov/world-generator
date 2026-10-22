#include "wg/io/export.hpp"

#include <lodepng.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef WG_HAVE_EXR
#include <tinyexr.h>
#endif

#include "io/json.hpp"
#include "io/layers_json.hpp"

namespace wg::io {

namespace {

using core::FieldId;
using core::StatusCode;

template <class T>
core::Expected<std::vector<double>> values_of(const core::WorldState& state, FieldId id) {
  core::Expected<core::FieldView<const T>> view = state.read<T>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  std::vector<double> v(view->data.begin(), view->data.end());
  return v;
}

// The field as doubles (exact for every dtype).
core::Expected<std::vector<double>> field_values(const core::WorldState& state, FieldId id) {
  switch (core::field_info(id).dtype) {
    case core::DType::u8:
      return values_of<std::uint8_t>(state, id);
    case core::DType::u16:
      return values_of<std::uint16_t>(state, id);
    case core::DType::i32:
      return values_of<std::int32_t>(state, id);
    case core::DType::f32:
      return values_of<float>(state, id);
  }
  return core::make_error(StatusCode::internal, "unknown dtype");
}

// Shortest text that reads back to the same double, independent of the locale.
std::string number_text(double v) { return Json(v).dump(); }

core::Status write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return core::make_status(StatusCode::io_error, "cannot create " + path.generic_string());
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) {
    return core::make_status(StatusCode::io_error, "cannot write " + path.generic_string());
  }
  return {};
}

}  // namespace

std::uint16_t png16_value(double x, double lo, double hi) noexcept {
  if (!std::isfinite(x) || !(hi > lo)) {
    return 0;
  }
  const double v = std::floor((((x - lo) / (hi - lo)) * 65535.0) + 0.5);
  return static_cast<std::uint16_t>(std::clamp(v, 0.0, 65535.0));
}

core::Expected<Png16Image> encode_png16(const core::WorldState& state, FieldId id,
                                        const Png16Options& options) noexcept {
  try {
    const core::FieldInfo& info = core::field_info(id);
    if (options.range && !(std::isfinite(options.range->lo) && std::isfinite(options.range->hi) &&
                           options.range->lo < options.range->hi)) {
      return core::make_error(StatusCode::invalid_argument,
                              "PNG16 range must be finite with lo < hi");
    }
    core::Expected<std::vector<double>> values = field_values(state, id);
    if (!values) {
      return core::make_error(std::move(values.error()));
    }

    Png16Image image;
    if (options.range) {
      image.lo = options.range->lo;
      image.hi = options.range->hi;
    } else if (info.range.has_min && info.range.has_max) {
      image.lo = info.range.min;
      image.hi = info.range.max;
    } else if (info.dtype == core::DType::u8 || info.dtype == core::DType::u16) {
      image.lo = 0.0;
      image.hi = 65535.0;
      image.scaled = false;
    } else {
      image.lo = std::numeric_limits<double>::infinity();
      image.hi = -std::numeric_limits<double>::infinity();
      for (const double v : *values) {
        if (std::isfinite(v)) {
          image.lo = std::min(image.lo, v);
          image.hi = std::max(image.hi, v);
        }
      }
      if (!(image.hi >= image.lo)) {  // no finite value
        image.lo = 0.0;
        image.hi = 0.0;
      }
    }

    // 16-bit samples are big-endian in PNG.
    std::vector<unsigned char> pixels(values->size() * 2);
    for (std::size_t i = 0; i < values->size(); ++i) {
      const double x = (*values)[i];
      const std::uint16_t g =
          image.scaled ? png16_value(x, image.lo, image.hi) : static_cast<std::uint16_t>(x);
      pixels[2 * i] = static_cast<unsigned char>(g >> 8);
      pixels[(2 * i) + 1] = static_cast<unsigned char>(g & 0xFFU);
    }

    lodepng::State png;
    png.info_raw.colortype = LCT_GREY;
    png.info_raw.bitdepth = 16;
    png.info_png.color.colortype = LCT_GREY;
    png.info_png.color.bitdepth = 16;
    png.encoder.auto_convert = 0;  // always 16-bit grey, whatever the values
    const std::string name(info.name);
    const std::string lo = number_text(image.lo);
    const std::string hi = number_text(image.hi);
    unsigned error = lodepng_add_text(&png.info_png, "wg:field", name.c_str());
    if (error == 0) {
      error = lodepng_add_text(&png.info_png, "wg:lo", lo.c_str());
    }
    if (error == 0) {
      error = lodepng_add_text(&png.info_png, "wg:hi", hi.c_str());
    }
    std::vector<unsigned char> out;
    if (error == 0) {
      const auto n = static_cast<unsigned>(state.grid().n);
      error = lodepng::encode(out, pixels, n, n, png);
    }
    if (error != 0) {
      return core::make_error(StatusCode::internal,
                              std::string("PNG encoding failed: ") + lodepng_error_text(error));
    }
    image.png.resize(out.size());
    if (!out.empty()) {
      std::memcpy(image.png.data(), out.data(), out.size());
    }
    return image;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("PNG encoding failed: ") + e.what());
  }
}

core::Status export_png16(const std::filesystem::path& path, const core::WorldState& state,
                          FieldId id, const Png16Options& options) noexcept {
  try {
    core::Expected<Png16Image> image = encode_png16(state, id, options);
    if (!image) {
      return std::move(image.error());
    }
    return write_bytes(path, image->png);
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::io_error,
                             "writing " + path.generic_string() + " failed: " + e.what());
  }
}

bool exr_supported() noexcept {
#ifdef WG_HAVE_EXR
  return true;
#else
  return false;
#endif
}

core::Expected<std::vector<std::byte>> encode_exr(const core::WorldState& state,
                                                  FieldId id) noexcept {
#ifdef WG_HAVE_EXR
  try {
    core::Expected<std::vector<double>> values = field_values(state, id);
    if (!values) {
      return core::make_error(std::move(values.error()));
    }
    std::vector<float> data(values->size());
    std::transform(values->begin(), values->end(), data.begin(),
                   [](double v) { return static_cast<float>(v); });

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);
    std::array<float*, 1> channels_data = {data.data()};
    image.images = reinterpret_cast<unsigned char**>(channels_data.data());
    image.width = state.grid().n;
    image.height = state.grid().n;
    image.num_channels = 1;

    EXRChannelInfo channel{};
    channel.name[0] = 'Y';
    channel.pixel_type = TINYEXR_PIXELTYPE_FLOAT;
    int pixel_type = TINYEXR_PIXELTYPE_FLOAT;
    int requested_type = TINYEXR_PIXELTYPE_FLOAT;
    header.num_channels = 1;
    header.channels = &channel;
    header.pixel_types = &pixel_type;
    header.requested_pixel_types = &requested_type;
    header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;

    unsigned char* memory = nullptr;
    const char* err = nullptr;
    const std::size_t size = SaveEXRImageToMemory(&image, &header, &memory, &err);
    if (size == 0) {
      std::string message = "EXR encoding failed";
      if (err != nullptr) {
        message += std::string(": ") + err;
        FreeEXRErrorMessage(err);
      }
      std::free(memory);
      return core::make_error(StatusCode::internal, message);
    }
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), memory, size);
    std::free(memory);
    return out;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("EXR encoding failed: ") + e.what());
  }
#else
  static_cast<void>(state);
  static_cast<void>(id);
  return core::make_error(StatusCode::failed_precondition,
                          "EXR export needs a library built with WG_WITH_EXR");
#endif
}

core::Status export_exr(const std::filesystem::path& path, const core::WorldState& state,
                        FieldId id) noexcept {
  try {
    core::Expected<std::vector<std::byte>> exr = encode_exr(state, id);
    if (!exr) {
      return std::move(exr.error());
    }
    return write_bytes(path, *exr);
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::io_error,
                             "writing " + path.generic_string() + " failed: " + e.what());
  }
}

core::Expected<std::string> layers_to_json(const core::VectorLayers& layers) noexcept {
  core::Expected<Json> json = detail::encode_layers(layers);
  if (!json) {
    return core::make_error(std::move(json.error()));
  }
  return dump_json(*json);
}

core::Status export_layers_json(const std::filesystem::path& path,
                                const core::VectorLayers& layers) noexcept {
  try {
    core::Expected<std::string> text = layers_to_json(layers);
    if (!text) {
      return std::move(text.error());
    }
    const auto* bytes = reinterpret_cast<const std::byte*>(text->data());
    return write_bytes(path, std::span<const std::byte>(bytes, text->size()));
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::io_error,
                             "writing " + path.generic_string() + " failed: " + e.what());
  }
}

}  // namespace wg::io
