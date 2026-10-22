#include "wg/io/export.hpp"

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <vector>

#ifdef WG_HAVE_EXR
#include <tinyexr.h>
#endif

#include "io/json.hpp"
#include "io/layers_json.hpp"
#include "support/sample_world.hpp"
#include "support/temp_dir.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::io::Png16Options;
using wg::io::Png16Range;

namespace {

struct Decoded {
  unsigned width = 0;
  unsigned height = 0;
  std::vector<std::uint16_t> grey;
  std::map<std::string, std::string> text;
};

Decoded decode_png16(const std::vector<std::byte>& png) {
  lodepng::State state;
  state.info_raw.colortype = LCT_GREY;
  state.info_raw.bitdepth = 16;
  std::vector<unsigned char> raw;
  Decoded d;
  const auto* bytes = reinterpret_cast<const unsigned char*>(png.data());
  REQUIRE(lodepng::decode(raw, d.width, d.height, state, bytes, png.size()) == 0);
  CHECK(state.info_png.color.colortype == LCT_GREY);
  CHECK(state.info_png.color.bitdepth == 16);
  d.grey.reserve(raw.size() / 2);
  for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
    d.grey.push_back(static_cast<std::uint16_t>((raw[i] << 8) | raw[i + 1]));
  }
  for (std::size_t i = 0; i < state.info_png.text_num; ++i) {
    d.text[state.info_png.text_keys[i]] = state.info_png.text_strings[i];
  }
  return d;
}

template <class T>
std::vector<double> values(const wg::core::WorldState& state, FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
  std::vector<std::byte> b(text.size());
  std::memcpy(b.data(), text.data(), text.size());
  return b;
}

}  // namespace

TEST_CASE("io.export png16_value scales, rounds and clamps") {
  CHECK(wg::io::png16_value(0.0, 0.0, 400.0) == 0);
  CHECK(wg::io::png16_value(400.0, 0.0, 400.0) == 65535);
  CHECK(wg::io::png16_value(200.0, 0.0, 400.0) == 32768);  // 32767.5 rounds up
  CHECK(wg::io::png16_value(-5.0, 0.0, 400.0) == 0);
  CHECK(wg::io::png16_value(1e9, 0.0, 400.0) == 65535);
  CHECK(wg::io::png16_value(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0) == 0);
  CHECK(wg::io::png16_value(1.0, 2.0, 2.0) == 0);
}

// --- Acceptance: PNG16 values equal the scaled field ----------------------------------------

TEST_CASE("io.export PNG16 values equal the field scaled by its registry range") {
  const wg::core::WorldState state = wg::test::sample_world();
  auto image = wg::io::encode_png16(state, FieldId::elevation);
  REQUIRE(image);
  CHECK(image->lo == 0.0);
  CHECK(image->hi == 400.0);  // elevation's registry range
  CHECK(image->scaled);
  const Decoded d = decode_png16(image->png);
  CHECK(d.width == 17);
  CHECK(d.height == 17);
  const std::vector<double> z = values<float>(state, FieldId::elevation);
  REQUIRE(d.grey.size() == z.size());
  for (std::size_t i = 0; i < z.size(); ++i) {
    CAPTURE(i);
    // Pixel i is cell i: row-major, row 0 at the top.
    const auto expected =
        static_cast<std::uint16_t>(std::lround(std::clamp(z[i] / 400.0, 0.0, 1.0) * 65535.0));
    CHECK(d.grey[i] == expected);
    CHECK(d.grey[i] == wg::io::png16_value(z[i], 0.0, 400.0));
  }
  CHECK(d.text.at("wg:field") == "elevation");
  CHECK(d.text.at("wg:lo") == "0.0");
  CHECK(d.text.at("wg:hi") == "400.0");
}

TEST_CASE("io.export PNG16 with an explicit range clamps outside it") {
  const wg::core::WorldState state = wg::test::sample_world();
  auto image = wg::io::encode_png16(state, FieldId::elevation,
                                    Png16Options{.range = Png16Range{.lo = 100.0, .hi = 200.0}});
  REQUIRE(image);
  const Decoded d = decode_png16(image->png);
  const std::vector<double> z = values<float>(state, FieldId::elevation);
  bool saw_low = false;
  bool saw_high = false;
  for (std::size_t i = 0; i < z.size(); ++i) {
    CHECK(d.grey[i] == wg::io::png16_value(z[i], 100.0, 200.0));
    saw_low = saw_low || (z[i] < 100.0 && d.grey[i] == 0);
    saw_high = saw_high || (z[i] > 200.0 && d.grey[i] == 65535);
  }
  CHECK(saw_low);
  CHECK(saw_high);

  CHECK(wg::io::encode_png16(state, FieldId::elevation,
                             Png16Options{.range = Png16Range{.lo = 2.0, .hi = 2.0}})
            .error()
            .code == StatusCode::invalid_argument);
  CHECK(wg::io::encode_png16(
            state, FieldId::elevation,
            Png16Options{.range =
                             Png16Range{.lo = 0.0, .hi = std::numeric_limits<double>::infinity()}})
            .error()
            .code == StatusCode::invalid_argument);
}

TEST_CASE("io.export PNG16 of a field without a registry range uses the data range") {
  const wg::core::WorldState state = wg::test::sample_world();
  // flow_accum has no upper bound; plate_id (i32) none at all.
  for (const FieldId id : {FieldId::flow_accum, FieldId::plate_id}) {
    CAPTURE(wg::core::field_info(id).name);
    auto image = wg::io::encode_png16(state, id);
    REQUIRE(image);
    const std::vector<double> v =
        id == FieldId::flow_accum ? values<float>(state, id) : values<std::int32_t>(state, id);
    CHECK(image->lo == *std::min_element(v.begin(), v.end()));
    CHECK(image->hi == *std::max_element(v.begin(), v.end()));
    const Decoded d = decode_png16(image->png);
    for (std::size_t i = 0; i < v.size(); ++i) {
      CHECK(d.grey[i] == wg::io::png16_value(v[i], image->lo, image->hi));
    }
    CHECK(*std::min_element(d.grey.begin(), d.grey.end()) == 0);
    CHECK(*std::max_element(d.grey.begin(), d.grey.end()) == 65535);
  }
}

TEST_CASE("io.export PNG16 stores u16 fields without a range unscaled") {
  const wg::core::WorldState state = wg::test::sample_world();
  auto image = wg::io::encode_png16(state, FieldId::biome);  // u16, no registry range
  REQUIRE(image);
  CHECK_FALSE(image->scaled);
  const Decoded d = decode_png16(image->png);
  const std::vector<double> v = values<std::uint16_t>(state, FieldId::biome);
  for (std::size_t i = 0; i < v.size(); ++i) {
    CHECK(d.grey[i] == static_cast<std::uint16_t>(v[i]));
  }

  // An enumerated u8 field has registry bounds (water: 0 to 3) and is scaled to them.
  image = wg::io::encode_png16(state, FieldId::water);
  REQUIRE(image);
  CHECK(image->scaled);
  CHECK(image->hi == 3.0);
  const Decoded w = decode_png16(image->png);
  const std::vector<double> water = values<std::uint8_t>(state, FieldId::water);
  for (std::size_t i = 0; i < water.size(); ++i) {
    CHECK(w.grey[i] == wg::io::png16_value(water[i], 0.0, 3.0));
  }
}

TEST_CASE("io.export PNG16 of a missing field is not_found; files equal the encoding") {
  const wg::core::WorldState state = wg::test::sample_world();
  CHECK(wg::io::encode_png16(state, FieldId::rainfall).error().code == StatusCode::not_found);
  const wg::test::TempDir dir;
  REQUIRE(wg::io::export_png16(dir / "e.png", state, FieldId::elevation).ok());
  auto image = wg::io::encode_png16(state, FieldId::elevation);
  REQUIRE(image);
  CHECK(read_file(dir / "e.png") == image->png);
  CHECK(wg::io::export_png16(dir / "no" / "e.png", state, FieldId::elevation).code ==
        StatusCode::io_error);
}

TEST_CASE("io.export layers JSON is the metadata form") {
  const wg::core::VectorLayers layers = wg::test::sample_layers();
  auto text = wg::io::layers_to_json(layers);
  REQUIRE(text);
  auto json = wg::io::parse_json(*text);
  REQUIRE(json);
  auto back = wg::io::detail::decode_layers(*json, "$");
  REQUIRE(back);
  CHECK(*back == layers);

  const wg::test::TempDir dir;
  REQUIRE(wg::io::export_layers_json(dir / "layers.json", layers).ok());
  const std::vector<std::byte> file = read_file(dir / "layers.json");
  CHECK(std::string(reinterpret_cast<const char*>(file.data()), file.size()) == *text);

  wg::core::VectorLayers bad = layers;
  bad.regions[0].centroid_x = std::numeric_limits<double>::quiet_NaN();
  CHECK(wg::io::layers_to_json(bad).error().code == StatusCode::invalid_argument);
  CHECK(wg::io::export_layers_json(dir / "bad.json", bad).code == StatusCode::invalid_argument);
}

TEST_CASE("io.export EXR holds the unscaled float values") {
  const wg::core::WorldState state = wg::test::sample_world();
  auto exr = wg::io::encode_exr(state, FieldId::elevation);
#ifdef WG_HAVE_EXR
  CHECK(wg::io::exr_supported());
  REQUIRE(exr);
  const auto* memory = reinterpret_cast<const unsigned char*>(exr->data());
  EXRVersion version;
  REQUIRE(ParseEXRVersionFromMemory(&version, memory, exr->size()) == TINYEXR_SUCCESS);
  EXRHeader header;
  InitEXRHeader(&header);
  const char* err = nullptr;
  REQUIRE(ParseEXRHeaderFromMemory(&header, &version, memory, exr->size(), &err) ==
          TINYEXR_SUCCESS);
  REQUIRE(header.num_channels == 1);
  CHECK(std::string(header.channels[0].name) == "Y");
  CHECK(header.pixel_types[0] == TINYEXR_PIXELTYPE_FLOAT);
  EXRImage image;
  InitEXRImage(&image);
  REQUIRE(LoadEXRImageFromMemory(&image, &header, memory, exr->size(), &err) == TINYEXR_SUCCESS);
  CHECK(image.width == 17);
  CHECK(image.height == 17);
  const auto* pixels = reinterpret_cast<const float*>(image.images[0]);
  const auto z = state.read<float>(FieldId::elevation);
  REQUIRE(z);
  CHECK(std::memcmp(pixels, z->data.data(), z->data.size_bytes()) == 0);
  FreeEXRImage(&image);
  FreeEXRHeader(&header);

  // Integer fields are converted to float.
  CHECK(wg::io::encode_exr(state, FieldId::plate_id));
  CHECK(wg::io::encode_exr(state, FieldId::rainfall).error().code == StatusCode::not_found);
  const wg::test::TempDir dir;
  REQUIRE(wg::io::export_exr(dir / "e.exr", state, FieldId::elevation).ok());
  CHECK(read_file(dir / "e.exr") == *exr);
#else
  CHECK_FALSE(wg::io::exr_supported());
  REQUIRE_FALSE(exr);
  CHECK(exr.error().code == StatusCode::failed_precondition);
  const wg::test::TempDir dir;
  CHECK(wg::io::export_exr(dir / "e.exr", state, FieldId::elevation).code ==
        StatusCode::failed_precondition);
#endif
}
