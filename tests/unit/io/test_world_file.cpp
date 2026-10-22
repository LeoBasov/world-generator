#include "wg/io/world_file.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/sample_world.hpp"
#include "support/temp_dir.hpp"
#include "wg/core/version.hpp"
#include "wg/io/byte_source.hpp"
#include "wg/params/hash.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::io::ChunkEntry;
using wg::io::Codec;
using wg::io::WorldReader;
using wg::io::WriteOptions;
using wg::test::sample_params;
using wg::test::sample_world;

namespace {

constexpr std::size_t kCellCount = std::size_t{17} * 17;  // the sample grid

std::vector<std::byte> write(const wg::core::WorldState& state, const WriteOptions& options = {}) {
  auto bytes = wg::io::write_world(state, sample_params(), options);
  REQUIRE(bytes);
  return std::move(*bytes);
}

std::uint64_t get_le(std::span<const std::byte> b, std::size_t offset, std::size_t size) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < size; ++i) {
    v |= static_cast<std::uint64_t>(b[offset + i]) << (8 * i);
  }
  return v;
}

void put_le(std::span<std::byte> b, std::size_t offset, std::size_t size, std::uint64_t v) {
  for (std::size_t i = 0; i < size; ++i) {
    b[offset + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFU);
  }
}

// Offset of table entry i in the file.
std::size_t entry_offset(std::span<const std::byte> file, std::size_t i) {
  return static_cast<std::size_t>(get_le(file, 52, 8)) + (i * wg::io::kChunkEntrySize);
}

// Index of the table entry named name.
std::size_t entry_index(const std::vector<std::byte>& file, std::string_view name) {
  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  const auto& chunks = reader->chunks();
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    if (chunks[i].name == name) {
      return i;
    }
  }
  FAIL("no chunk " << name);
  return 0;
}

void check_same_fields(const wg::core::WorldState& a, const wg::core::WorldState& b) {
  using wg::test::field_bytes;
  for (std::size_t i = 0; i < wg::core::kFieldCount; ++i) {
    const auto id = static_cast<FieldId>(i);
    CAPTURE(wg::core::field_info(id).name);
    REQUIRE(a.has(id) == b.has(id));
    if (!a.has(id)) {
      continue;
    }
    std::span<const std::byte> x;
    std::span<const std::byte> y;
    switch (wg::core::field_info(id).dtype) {
      case wg::core::DType::u8:
        x = field_bytes<std::uint8_t>(a, id);
        y = field_bytes<std::uint8_t>(b, id);
        break;
      case wg::core::DType::u16:
        x = field_bytes<std::uint16_t>(a, id);
        y = field_bytes<std::uint16_t>(b, id);
        break;
      case wg::core::DType::i32:
        x = field_bytes<std::int32_t>(a, id);
        y = field_bytes<std::int32_t>(b, id);
        break;
      case wg::core::DType::f32:
        x = field_bytes<float>(a, id);
        y = field_bytes<float>(b, id);
        break;
    }
    REQUIRE(x.size() == y.size());
    CHECK(std::memcmp(x.data(), y.data(), x.size()) == 0);
  }
}

// ByteSource that records every range read.
class RecordingSource final : public wg::io::ByteSource {
 public:
  explicit RecordingSource(std::span<const std::byte> data) : inner_(data) {}

  [[nodiscard]] std::uint64_t size() const noexcept override { return inner_.size(); }
  wg::core::Status read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override {
    reads.emplace_back(offset, out.size());
    return inner_.read_at(offset, out);
  }

  std::vector<std::pair<std::uint64_t, std::uint64_t>> reads;

 private:
  wg::io::MemorySource inner_;
};

wg::core::Status open_status(const std::vector<std::byte>& file) {
  auto r = WorldReader::open_buffer(file);
  return r ? wg::core::Status{} : r.error();
}

}  // namespace

// --- Acceptance: round trip bit-identical ------------------------------------------------------

TEST_CASE("io.world_file round trip is bit-identical with both codecs") {
  const wg::core::WorldState state = sample_world();
  for (const Codec codec : {Codec::zstd, Codec::raw}) {
    CAPTURE(static_cast<int>(codec));
    const std::vector<std::byte> file = write(state, WriteOptions{.codec = codec});
    auto loaded = wg::io::read_world_buffer(file);
    REQUIRE(loaded);
    check_same_fields(state, loaded->state);
    CHECK(loaded->state.grid() == state.grid());
    CHECK(loaded->state.layers() == state.layers());
    CHECK(loaded->state.provenance() == state.provenance());
    CHECK(loaded->params == sample_params());
    CHECK(wg::params::params_hash(loaded->params) == state.provenance().params_hash);
    // -0.0 and the denormal keep their bits.
    CHECK(std::signbit(loaded->state.layers().peaks[1].height_eu));
    CHECK(loaded->state.layers().peaks[1].radius_cells == std::numeric_limits<float>::denorm_min());
    // Writing what was read gives the same bytes.
    CHECK(write(loaded->state, WriteOptions{.codec = codec}) == file);
  }
}

TEST_CASE("io.world_file writing is deterministic and zstd shrinks smooth fields") {
  const wg::core::WorldState state = sample_world();
  const std::vector<std::byte> a = write(state);
  CHECK(write(state) == a);
  const std::vector<std::byte> raw = write(state, WriteOptions{.codec = Codec::raw});
  CHECK(a.size() < raw.size());
  auto reader = WorldReader::open_buffer(a);
  REQUIRE(reader);
  const ChunkEntry* lat = reader->chunk(FieldId::latitude);
  REQUIRE(lat != nullptr);
  CHECK(lat->codec == Codec::zstd);
  CHECK(lat->stored_size < lat->raw_size);
  auto raw_reader = WorldReader::open_buffer(raw);
  REQUIRE(raw_reader);
  for (const ChunkEntry& e : raw_reader->chunks()) {
    CHECK(e.codec == Codec::raw);
    CHECK(e.stored_size == e.raw_size);
  }
}

TEST_CASE("io.world_file header and table layout") {
  const wg::core::WorldState state = sample_world();
  const std::vector<std::byte> file = write(state);
  const std::span<const std::byte> b(file);
  CHECK(std::memcmp(b.data(), "WGW\0", 4) == 0);
  CHECK(get_le(b, 4, 2) == wg::io::kWorldFormatVersion);
  CHECK(get_le(b, 6, 2) == 0);
  CHECK(get_le(b, 8, 4) == 17);
  CHECK(std::bit_cast<double>(get_le(b, 12, 8)) == 2500.0);
  CHECK(std::bit_cast<double>(get_le(b, 20, 8)) == 12.5);
  CHECK(get_le(b, 28, 8) == wg::test::kSampleSeed);
  CHECK(get_le(b, 36, 8) == wg::params::params_hash(sample_params()));
  const wg::core::Version v = wg::core::version();
  CHECK(get_le(b, 44, 4) ==
        ((static_cast<std::uint64_t>(v.major) << 20) | (static_cast<std::uint64_t>(v.minor) << 10) |
         static_cast<std::uint64_t>(v.patch)));
  const std::uint64_t count = get_le(b, 48, 4);
  CHECK(count == 7);  // six fields and the metadata
  CHECK(get_le(b, 52, 8) + (count * wg::io::kChunkEntrySize) == file.size());
  CHECK(get_le(b, 60, 4) == 0);

  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  const wg::io::WorldHeader& h = reader->header();
  CHECK(h.grid == state.grid());
  CHECK(h.seed == wg::test::kSampleSeed);
  CHECK(h.library_version == v);
  CHECK(h.chunk_count == 7);
  // Fields in id order, then the metadata.
  std::vector<std::string> names;
  names.reserve(reader->chunks().size());
  for (const ChunkEntry& e : reader->chunks()) {
    names.push_back(e.name);
  }
  CHECK(names == std::vector<std::string>{"latitude", "elevation", "flow_accum", "water", "biome",
                                          "plate_id", "$metadata"});
  CHECK(reader->chunks().back().dtype == wg::io::kMetadataDType);
  // The first entry: zero-padded name, then dtype, codec and reserved bytes.
  const std::size_t e0 = entry_offset(b, 0);
  CHECK(std::memcmp(b.data() + e0, "latitude", 8) == 0);
  CHECK(std::all_of(b.begin() + static_cast<std::ptrdiff_t>(e0 + 8),
                    b.begin() + static_cast<std::ptrdiff_t>(e0 + 32),
                    [](std::byte x) { return x == std::byte{0}; }));
  CHECK(get_le(b, e0 + 32, 1) == static_cast<std::uint64_t>(wg::core::DType::f32));
  CHECK(get_le(b, e0 + 34, 2) == 0);
  CHECK(get_le(b, e0 + 36, 8) == kCellCount * 4);
  CHECK(get_le(b, e0 + 52, 8) == wg::io::kWorldHeaderSize);
}

TEST_CASE("io.world_file file round trip through a temporary and rename") {
  const wg::test::TempDir dir;
  const wg::core::WorldState state = sample_world();
  const auto path = dir / "world.wgw";
  REQUIRE(wg::io::write_world_file(path, state, sample_params()).ok());
  CHECK_FALSE(std::filesystem::exists(dir / "world.wgw.tmp"));

  {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> text((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    std::vector<std::byte> on_disk(text.size());
    std::memcpy(on_disk.data(), text.data(), text.size());
    CHECK(on_disk == write(state));
  }

  auto loaded = wg::io::read_world_file(path);
  REQUIRE(loaded);
  check_same_fields(state, loaded->state);
  CHECK(loaded->state.layers() == state.layers());

  // Overwriting an existing file works too.
  REQUIRE(wg::io::write_world_file(path, state, sample_params()).ok());

  auto missing = wg::io::read_world_file(dir / "missing.wgw");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == StatusCode::io_error);
  CHECK(wg::io::write_world_file(dir / "no" / "dir" / "w.wgw", state, sample_params()).code ==
        StatusCode::io_error);
}

// --- Acceptance: reading one field touches only its chunk ----------------------------------

TEST_CASE("io.world_file reading one field touches only its chunk") {
  const std::vector<std::byte> file = write(sample_world());
  RecordingSource source(file);
  auto reader = WorldReader::open(source);
  REQUIRE(reader);

  // open: header, table, metadata chunk.
  const ChunkEntry& meta = reader->chunks().back();
  const std::uint64_t table = reader->header().chunk_table_offset;
  REQUIRE(source.reads.size() == 3);
  CHECK(source.reads[0] == std::pair<std::uint64_t, std::uint64_t>{0, 64});
  CHECK(source.reads[1] == std::pair<std::uint64_t, std::uint64_t>{table, file.size() - table});
  CHECK(source.reads[2] == std::pair<std::uint64_t, std::uint64_t>{meta.offset, meta.stored_size});

  source.reads.clear();
  const ChunkEntry* elevation = reader->chunk(FieldId::elevation);
  REQUIRE(elevation != nullptr);
  std::vector<std::byte> out(elevation->raw_size);
  REQUIRE(reader->read_field(FieldId::elevation, out).ok());
  REQUIRE(source.reads.size() == 1);
  CHECK(source.reads[0] ==
        std::pair<std::uint64_t, std::uint64_t>{elevation->offset, elevation->stored_size});

  // A partial load reads only the requested fields.
  source.reads.clear();
  const wg::pipeline::FieldSet only{FieldId::elevation, FieldId::water};
  auto loaded = wg::io::read_world(source, wg::io::ReadOptions{.fields = &only});
  REQUIRE(loaded);
  CHECK(source.reads.size() == 3 + 2);
  CHECK(loaded->state.has(FieldId::elevation));
  CHECK(loaded->state.has(FieldId::water));
  CHECK_FALSE(loaded->state.has(FieldId::biome));
  CHECK(loaded->state.layers() == wg::test::sample_layers());
}

TEST_CASE("io.world_file read_into fills adopted buffers without copying") {
  const wg::core::WorldState state = sample_world();
  const std::vector<std::byte> file = write(state);
  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  auto target = wg::core::WorldState::create(state.grid());
  REQUIRE(target);
  std::vector<float> external(static_cast<std::size_t>(state.grid().cells()));
  REQUIRE(target->adopt(FieldId::elevation, std::span<float>(external)).ok());
  REQUIRE(reader->read_into(*target).ok());
  CHECK(target->is_adopted(FieldId::elevation));
  const auto original = state.read<float>(FieldId::elevation);
  REQUIRE(original);
  CHECK(std::equal(external.begin(), external.end(), original->data.begin()));

  auto other = wg::core::WorldState::create(wg::test::make_grid(33));
  REQUIRE(other);
  CHECK(reader->read_into(*other).code == StatusCode::invalid_argument);
}

TEST_CASE("io.world_file read_field checks the field and the buffer size") {
  const std::vector<std::byte> file = write(sample_world());
  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  std::vector<std::byte> out(kCellCount * 4);
  CHECK(reader->read_field(FieldId::rainfall, out).code == StatusCode::not_found);
  CHECK_FALSE(reader->has(FieldId::rainfall));
  CHECK(reader->chunk(FieldId::rainfall) == nullptr);
  std::vector<std::byte> small(10);
  CHECK(reader->read_field(FieldId::elevation, small).code == StatusCode::invalid_argument);
}

// --- Acceptance: corrupted chunk detected by XXH3 ------------------------------------------

TEST_CASE("io.world_file a corrupted chunk is detected by XXH3") {
  std::vector<std::byte> file = write(sample_world(), WriteOptions{.codec = Codec::raw});
  const ChunkEntry elevation = [&] {
    auto reader = WorldReader::open_buffer(file);
    REQUIRE(reader);
    return *reader->chunk(FieldId::elevation);
  }();
  file[elevation.offset + 100] ^= std::byte{0x01};

  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);  // the table and metadata are intact
  std::vector<std::byte> out(elevation.raw_size);
  const wg::core::Status s = reader->read_field(FieldId::elevation, out);
  CHECK(s.code == StatusCode::parse_error);
  CHECK(s.message.find("chunk elevation is corrupt: XXH3") != std::string::npos);
  // Other fields still read.
  std::vector<std::byte> water(kCellCount);
  CHECK(reader->read_field(FieldId::water, water).ok());
  CHECK(wg::io::read_world_buffer(file).error().code == StatusCode::parse_error);
}

TEST_CASE("io.world_file a corrupted zstd chunk or metadata is refused") {
  std::vector<std::byte> file = write(sample_world());
  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  const ChunkEntry& latitude = *reader->chunk(FieldId::latitude);
  const ChunkEntry& meta = reader->chunks().back();

  std::vector<std::byte> bad = file;
  bad[latitude.offset + (latitude.stored_size / 2)] ^= std::byte{0x40};
  auto r = WorldReader::open_buffer(bad);
  REQUIRE(r);
  std::vector<std::byte> out(latitude.raw_size);
  const wg::core::Status s = r->read_field(FieldId::latitude, out);
  CHECK(s.code == StatusCode::parse_error);
  CHECK(s.message.find("chunk latitude") != std::string::npos);

  bad = file;
  bad[meta.offset + (meta.stored_size / 2)] ^= std::byte{0x40};
  CHECK(open_status(bad).code == StatusCode::parse_error);
}

// --- Acceptance: future-version file refused -------------------------------------------------

TEST_CASE("io.world_file a future format version is refused") {
  std::vector<std::byte> file = write(sample_world());
  put_le(file, 4, 2, wg::io::kWorldFormatVersion + 1);
  const wg::core::Status s = open_status(file);
  CHECK(s.code == StatusCode::unsupported_version);
  CHECK(s.message == "world file: format version 2 is newer than the supported 1");
  CHECK(wg::io::read_world_buffer(file).error().code == StatusCode::unsupported_version);
}

TEST_CASE("io.world_file malformed headers and tables are parse errors") {
  const std::vector<std::byte> good = write(sample_world());
  REQUIRE(open_status(good).ok());
  const auto refused = [&](const auto& patch) {
    std::vector<std::byte> f = good;
    patch(f);
    return open_status(f).code == StatusCode::parse_error;
  };
  CHECK(refused([](auto& f) { f[0] = std::byte{'X'}; }));                      // magic
  CHECK(refused([](auto& f) { put_le(f, 4, 2, 0); }));                         // version 0
  CHECK(refused([](auto& f) { put_le(f, 6, 2, 1); }));                         // flags
  CHECK(refused([](auto& f) { put_le(f, 8, 4, 18); }));                        // grid n
  CHECK(refused([](auto& f) { put_le(f, 60, 4, 1); }));                        // padding
  CHECK(refused([](auto& f) { f.pop_back(); }));                               // truncated
  CHECK(refused([](auto& f) { f.push_back(std::byte{0}); }));                  // trailing byte
  CHECK(refused([](auto& f) { f.resize(40); }));                               // no header
  CHECK(refused([](auto& f) { put_le(f, 48, 4, 8); }));                        // chunk count
  CHECK(refused([](auto& f) { f[entry_offset(f, 0)] = std::byte{0x01}; }));    // name
  CHECK(refused([](auto& f) { put_le(f, entry_offset(f, 0) + 33, 1, 9); }));   // codec
  CHECK(refused([](auto& f) { put_le(f, entry_offset(f, 0) + 34, 2, 1); }));   // reserved
  CHECK(refused([](auto& f) { put_le(f, entry_offset(f, 0) + 32, 1, 0); }));   // dtype
  CHECK(refused([](auto& f) { put_le(f, entry_offset(f, 0) + 36, 8, 12); }));  // raw size
  CHECK(refused([](auto& f) { put_le(f, entry_offset(f, 0) + 52, 8, 10); }));  // offset < 64
  CHECK(refused([](auto& f) {                                                  // past the table
    put_le(f, entry_offset(f, 0) + 52, 8, get_le(f, 52, 8) - 1);
  }));
  CHECK(refused([](auto& f) {  // overlapping chunks
    put_le(f, entry_offset(f, 1) + 52, 8, get_le(f, entry_offset(f, 0) + 52, 8));
  }));
  CHECK(refused([](auto& f) {  // duplicate name
    std::memcpy(f.data() + entry_offset(f, 1), f.data() + entry_offset(f, 0), 32);
  }));
  CHECK(refused([](auto& f) {  // no metadata: rename it
    const std::size_t last = entry_offset(f, get_le(f, 48, 4) - 1);
    f[last] = std::byte{'x'};
  }));
}

TEST_CASE("io.world_file chunks of unknown fields are skipped and listed") {
  std::vector<std::byte> file = write(sample_world());
  const std::size_t biome = entry_index(file, "biome");
  const std::size_t e = entry_offset(file, biome);
  std::memset(file.data() + e, 0, 32);
  std::memcpy(file.data() + e, "zz_future_field", 15);
  auto reader = WorldReader::open_buffer(file);
  REQUIRE(reader);
  CHECK(reader->unknown_chunks() == std::vector<std::string>{"zz_future_field"});
  CHECK_FALSE(reader->has(FieldId::biome));
  auto loaded = wg::io::read_world_buffer(file);
  REQUIRE(loaded);
  CHECK_FALSE(loaded->state.has(FieldId::biome));
  CHECK(loaded->state.has(FieldId::plate_id));
}

TEST_CASE("io.world_file writer refuses inconsistent input") {
  wg::core::WorldState state = sample_world();
  // Parameters that do not match the provenance's hash.
  wg::params::GenParams other = sample_params();
  other.grid.dx_m = 1.0;
  auto r = wg::io::write_world(state, other);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::invalid_argument);
  CHECK(r.error().message.find("params_hash") != std::string::npos);

  CHECK(wg::io::write_world(state, sample_params(), WriteOptions{.zstd_level = 0}).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::io::write_world(state, sample_params(), WriteOptions{.zstd_level = 20}).error().code ==
        StatusCode::invalid_argument);

  state.layers().lakes[0].spill_elevation_eu = std::numeric_limits<float>::quiet_NaN();
  r = wg::io::write_world(state, sample_params());
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::invalid_argument);
  CHECK(r.error().message == "layer lakes[0].spill_elevation_eu is not finite");

  state.layers().lakes[0].spill_elevation_eu = 1.0F;
  state.provenance().rejections[0].measured = std::numeric_limits<double>::infinity();
  CHECK(wg::io::write_world(state, sample_params()).error().code == StatusCode::invalid_argument);
}

TEST_CASE("io.world_file a world without fields or layers round-trips") {
  auto created = wg::core::WorldState::create(wg::test::make_grid(17, 2500.0, 12.5));
  REQUIRE(created);
  created->provenance().params_hash = wg::params::params_hash(sample_params());
  const std::vector<std::byte> file = write(*created);
  auto loaded = wg::io::read_world_buffer(file);
  REQUIRE(loaded);
  CHECK(loaded->state.layers().empty());
  CHECK(loaded->state.provenance() == created->provenance());
  for (std::size_t i = 0; i < wg::core::kFieldCount; ++i) {
    CHECK_FALSE(loaded->state.has(static_cast<FieldId>(i)));
  }
}
