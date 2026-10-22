#include "wg/io/world_file.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "io/binary.hpp"
#include "io/json.hpp"
#include "io/layers_json.hpp"
#include "params/reader.hpp"
#include "wg/core/dtype.hpp"
#include "wg/io/hash.hpp"
#include "wg/params/hash.hpp"
#include "wg/params/load.hpp"
#include "wg/params/validation.hpp"

namespace wg::io {

namespace {

using core::FieldId;
using core::StatusCode;
using detail::ByteReader;
using detail::ByteWriter;

constexpr int kMinZstdLevel = 1;
constexpr int kMaxZstdLevel = 19;

core::Status file_error(StatusCode code, std::string_view message) {
  return core::make_status(code, "world file: " + std::string(message));
}

std::string hex64(std::uint64_t v) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = kDigits[v & 0xFU];
    v >>= 4;
  }
  return s;
}

std::optional<std::uint64_t> parse_hex64(std::string_view s) {
  if (s.size() != 16) {
    return std::nullopt;
  }
  std::uint64_t v = 0;
  for (char c : s) {
    std::uint64_t d = 0;
    if (c >= '0' && c <= '9') {
      d = static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<std::uint64_t>(c - 'a' + 10);
    } else {
      return std::nullopt;
    }
    v = (v << 4) | d;
  }
  return v;
}

// --- Library version: major (12 bits) . minor (10 bits) . patch (10 bits) in one u32 ----------

std::optional<std::uint32_t> pack_version(const core::Version& v) {
  if (v.major < 0 || v.minor < 0 || v.patch < 0 || v.major > 4095 || v.minor > 1023 ||
      v.patch > 1023) {
    return std::nullopt;
  }
  return (static_cast<std::uint32_t>(v.major) << 20) | (static_cast<std::uint32_t>(v.minor) << 10) |
         static_cast<std::uint32_t>(v.patch);
}

core::Version unpack_version(std::uint32_t v) {
  return core::Version{.major = static_cast<int>(v >> 20),
                       .minor = static_cast<int>((v >> 10) & 1023U),
                       .patch = static_cast<int>(v & 1023U)};
}

std::string version_text(const core::Version& v) {
  return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

std::optional<core::Version> parse_version(std::string_view s) {
  std::array<int, 3> parts{};
  std::size_t part = 0;
  bool digit = false;
  for (char c : s) {
    if (c == '.') {
      ++part;
      if (!digit || part == 3) {
        return std::nullopt;
      }
      digit = false;
    } else if (c >= '0' && c <= '9' && parts[part] < 100000) {
      parts[part] = (parts[part] * 10) + (c - '0');
      digit = true;
    } else {
      return std::nullopt;
    }
  }
  if (part != 2 || !digit) {
    return std::nullopt;
  }
  return core::Version{.major = parts[0], .minor = parts[1], .patch = parts[2]};
}

// --- Field bytes ------------------------------------------------------------------------------

template <class T>
std::span<const std::byte> bytes_of(const core::WorldState& state, FieldId id) noexcept {
  core::Expected<core::FieldView<const T>> view = state.read<T>(id);
  return view ? std::as_bytes(view->data) : std::span<const std::byte>{};
}

std::span<const std::byte> field_bytes(const core::WorldState& state, FieldId id) noexcept {
  switch (core::field_info(id).dtype) {
    case core::DType::u8:
      return bytes_of<std::uint8_t>(state, id);
    case core::DType::u16:
      return bytes_of<std::uint16_t>(state, id);
    case core::DType::i32:
      return bytes_of<std::int32_t>(state, id);
    case core::DType::f32:
      return bytes_of<float>(state, id);
  }
  return {};
}

template <class T>
core::Expected<std::span<std::byte>> writable_of(core::WorldState& state, FieldId id) noexcept {
  core::Expected<core::FieldView<T>> view = state.write<T>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return std::as_writable_bytes(view->data);
}

core::Expected<std::span<std::byte>> writable_bytes(core::WorldState& state, FieldId id) noexcept {
  switch (core::field_info(id).dtype) {
    case core::DType::u8:
      return writable_of<std::uint8_t>(state, id);
    case core::DType::u16:
      return writable_of<std::uint16_t>(state, id);
    case core::DType::i32:
      return writable_of<std::int32_t>(state, id);
    case core::DType::f32:
      return writable_of<float>(state, id);
  }
  return core::make_error(StatusCode::internal, "unknown dtype");
}

// --- Header and table encoding ----------------------------------------------------------------

std::vector<std::byte> encode_header(const WorldHeader& h, std::uint32_t packed_version) {
  std::vector<std::byte> b;
  b.reserve(kWorldHeaderSize);
  ByteWriter w(b);
  for (char c : kWorldMagic) {
    w.u8(static_cast<std::uint8_t>(c));
  }
  w.u16(h.format_version);
  w.u16(h.flags);
  w.i32(h.grid.n);
  w.f64(h.grid.dx_m);
  w.f64(h.grid.hu_m);
  w.u64(h.seed);
  w.u64(h.params_hash);
  w.u32(packed_version);
  w.u32(h.chunk_count);
  w.u64(h.chunk_table_offset);
  w.zeros(4);
  return b;
}

void encode_entry(const ChunkEntry& e, ByteWriter& w) {
  for (std::size_t i = 0; i < kChunkNameSize; ++i) {
    w.u8(static_cast<std::uint8_t>(i < e.name.size() ? e.name[i] : '\0'));
  }
  w.u8(e.dtype);
  w.u8(static_cast<std::uint8_t>(e.codec));
  w.u16(0);
  w.u64(e.raw_size);
  w.u64(e.stored_size);
  w.u64(e.offset);
  w.u64(e.checksum);
}

// --- Metadata ---------------------------------------------------------------------------------

core::Expected<Json> encode_provenance(const core::Provenance& p) {
  Json o = Json::object();
  o["profile"] = p.profile;
  o["seed"] = p.seed;
  o["params_hash"] = hex64(p.params_hash);
  o["library_version"] = version_text(p.library_version);
  o["attempt"] = p.attempt;
  o["completed_stages"] = p.completed_stages;
  Json rejections = Json::array();
  for (std::size_t i = 0; i < p.rejections.size(); ++i) {
    const core::RejectionRecord& r = p.rejections[i];
    if (!std::isfinite(r.measured) || !std::isfinite(r.required)) {
      return core::make_error(StatusCode::invalid_argument,
                              "provenance rejection " + std::to_string(i) +
                                  " has a non-finite measured or required value");
    }
    Json ro = Json::object();
    ro["attempt"] = r.attempt;
    ro["stage"] = r.stage;
    ro["check_id"] = r.check_id;
    ro["measured"] = r.measured;
    ro["required"] = r.required;
    rejections.push_back(std::move(ro));
  }
  o["rejections"] = std::move(rejections);
  return o;
}

core::Expected<std::string> encode_metadata(const core::WorldState& state,
                                            const params::GenParams& params) {
  core::Expected<Json> provenance = encode_provenance(state.provenance());
  if (!provenance) {
    return core::make_error(std::move(provenance.error()));
  }
  core::Expected<std::string> params_text =
      params::to_json(params::ParamsDocument{.params = params});
  if (!params_text) {
    return core::make_error(std::move(params_text.error()));
  }
  core::Expected<Json> params_json = parse_json(*params_text);
  if (!params_json) {
    return core::make_error(StatusCode::internal, "parameters: " + params_json.error().message);
  }
  core::Expected<Json> layers = detail::encode_layers(state.layers());
  if (!layers) {
    return core::make_error(std::move(layers.error()));
  }
  Json meta = Json::object();
  meta["schema_version"] = kWorldMetadataVersion;
  meta["provenance"] = std::move(*provenance);
  meta["params"] = std::move(*params_json);
  meta["layers"] = std::move(*layers);
  return dump_json(meta, -1);
}

// --- Writing ----------------------------------------------------------------------------------

// Destination of the writer: bytes are appended, then the header is patched at offset 0.
class Sink {
 public:
  virtual ~Sink() = default;
  virtual core::Status append(std::span<const std::byte> data) = 0;
  virtual core::Status patch_header(std::span<const std::byte> header) = 0;
  [[nodiscard]] std::uint64_t position() const noexcept { return position_; }

 protected:
  Sink() = default;
  Sink(const Sink&) = default;
  Sink& operator=(const Sink&) = default;
  Sink(Sink&&) = default;
  Sink& operator=(Sink&&) = default;

  std::uint64_t position_ = 0;
};

class VectorSink final : public Sink {
 public:
  explicit VectorSink(std::vector<std::byte>& out) : out_(&out) {}

  core::Status append(std::span<const std::byte> data) override {
    out_->insert(out_->end(), data.begin(), data.end());
    position_ += data.size();
    return {};
  }

  core::Status patch_header(std::span<const std::byte> header) override {
    std::memcpy(out_->data(), header.data(), header.size());
    return {};
  }

 private:
  std::vector<std::byte>* out_;
};

class FileSink final : public Sink {
 public:
  FileSink(std::ofstream& out, std::string name) : out_(&out), name_(std::move(name)) {}

  core::Status append(std::span<const std::byte> data) override {
    out_->write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    position_ += data.size();
    return checked();
  }

  core::Status patch_header(std::span<const std::byte> header) override {
    out_->seekp(0);
    out_->write(reinterpret_cast<const char*>(header.data()),
                static_cast<std::streamsize>(header.size()));
    return checked();
  }

 private:
  [[nodiscard]] core::Status checked() const {
    return *out_ ? core::Status{}
                 : core::make_status(StatusCode::io_error, "cannot write " + name_);
  }

  std::ofstream* out_;
  std::string name_;
};

struct CCtxDeleter {
  void operator()(ZSTD_CCtx* c) const noexcept { ZSTD_freeCCtx(c); }
};

// Compresses chunks one at a time and appends them to the sink.
class ChunkWriter {
 public:
  ChunkWriter(Sink& sink, const WriteOptions& options) : sink_(&sink), options_(options) {}

  core::Status add(std::string_view name, std::uint8_t dtype, std::span<const std::byte> raw) {
    ChunkEntry e{.name = std::string(name),
                 .dtype = dtype,
                 .codec = Codec::raw,
                 .raw_size = raw.size(),
                 .stored_size = raw.size(),
                 .offset = sink_->position(),
                 .checksum = xxh3_64(raw)};
    std::span<const std::byte> stored = raw;
    if (options_.codec == Codec::zstd) {
      if (!cctx_) {
        cctx_.reset(ZSTD_createCCtx());
        if (!cctx_) {
          return core::make_status(StatusCode::out_of_memory, "cannot create a zstd context");
        }
      }
      scratch_.resize(ZSTD_compressBound(raw.size()));
      const std::size_t size = ZSTD_compressCCtx(cctx_.get(), scratch_.data(), scratch_.size(),
                                                 raw.data(), raw.size(), options_.zstd_level);
      if (ZSTD_isError(size) != 0U) {
        return core::make_status(StatusCode::internal, "zstd compression of chunk " + e.name +
                                                           " failed: " + ZSTD_getErrorName(size));
      }
      if (size < raw.size()) {
        e.codec = Codec::zstd;
        e.stored_size = size;
        stored = std::span<const std::byte>(scratch_.data(), size);
      }
    }
    if (core::Status s = sink_->append(stored); !s.ok()) {
      return s;
    }
    entries_.push_back(std::move(e));
    return {};
  }

  [[nodiscard]] const std::vector<ChunkEntry>& entries() const noexcept { return entries_; }

 private:
  Sink* sink_;
  WriteOptions options_;
  std::unique_ptr<ZSTD_CCtx, CCtxDeleter> cctx_;
  std::vector<std::byte> scratch_;
  std::vector<ChunkEntry> entries_;
};

core::Status write_to(Sink& sink, const core::WorldState& state, const params::GenParams& params,
                      const WriteOptions& options) {
  if (options.codec != Codec::raw && options.codec != Codec::zstd) {
    return core::make_status(StatusCode::invalid_argument, "unknown codec");
  }
  if (options.zstd_level < kMinZstdLevel || options.zstd_level > kMaxZstdLevel) {
    return core::make_status(
        StatusCode::invalid_argument,
        "zstd level " + std::to_string(options.zstd_level) + " is outside [1, 19]");
  }
  const core::Provenance& prov = state.provenance();
  const std::uint64_t hash = params::params_hash(params);
  if (prov.params_hash != hash) {
    return core::make_status(StatusCode::invalid_argument,
                             "the provenance's params_hash " + hex64(prov.params_hash) +
                                 " differs from the parameters' hash " + hex64(hash));
  }
  const std::optional<std::uint32_t> packed = pack_version(core::version());
  if (!packed) {
    return core::make_status(StatusCode::internal, "library version does not fit the header");
  }
  core::Expected<std::string> metadata = encode_metadata(state, params);
  if (!metadata) {
    return std::move(metadata.error());
  }

  const std::array<std::byte, kWorldHeaderSize> placeholder{};
  if (core::Status s = sink.append(placeholder); !s.ok()) {
    return s;
  }
  ChunkWriter chunks(sink, options);
  for (std::size_t i = 0; i < core::kFieldCount; ++i) {
    const auto id = static_cast<FieldId>(i);
    if (!state.has(id)) {
      continue;
    }
    const core::FieldInfo& info = core::field_info(id);
    if (core::Status s =
            chunks.add(info.name, static_cast<std::uint8_t>(info.dtype), field_bytes(state, id));
        !s.ok()) {
      return s;
    }
  }
  const auto* meta_bytes = reinterpret_cast<const std::byte*>(metadata->data());
  if (core::Status s = chunks.add(kMetadataChunkName, kMetadataDType,
                                  std::span<const std::byte>(meta_bytes, metadata->size()));
      !s.ok()) {
    return s;
  }

  const WorldHeader header{.format_version = kWorldFormatVersion,
                           .flags = 0,
                           .grid = state.grid(),
                           .seed = prov.seed,
                           .params_hash = hash,
                           .library_version = core::version(),
                           .chunk_count = static_cast<std::uint32_t>(chunks.entries().size()),
                           .chunk_table_offset = sink.position()};
  std::vector<std::byte> table;
  table.reserve(chunks.entries().size() * kChunkEntrySize);
  ByteWriter w(table);
  for (const ChunkEntry& e : chunks.entries()) {
    encode_entry(e, w);
  }
  if (core::Status s = sink.append(table); !s.ok()) {
    return s;
  }
  return sink.patch_header(encode_header(header, *packed));
}

// --- Reading ----------------------------------------------------------------------------------

struct DCtxDeleter {
  void operator()(ZSTD_DCtx* c) const noexcept { ZSTD_freeDCtx(c); }
};

// Reads, decompresses and verifies one chunk into out (exactly its raw size).
core::Status read_chunk(ByteSource& source, const ChunkEntry& e, std::span<std::byte> out) {
  if (e.codec == Codec::raw) {
    if (core::Status s = source.read_at(e.offset, out); !s.ok()) {
      return s;
    }
  } else {
    std::vector<std::byte> stored(e.stored_size);
    if (core::Status s = source.read_at(e.offset, stored); !s.ok()) {
      return s;
    }
    const std::unique_ptr<ZSTD_DCtx, DCtxDeleter> dctx(ZSTD_createDCtx());
    if (!dctx) {
      return core::make_status(StatusCode::out_of_memory, "cannot create a zstd context");
    }
    const std::size_t size =
        ZSTD_decompressDCtx(dctx.get(), out.data(), out.size(), stored.data(), stored.size());
    if (ZSTD_isError(size) != 0U) {
      return file_error(StatusCode::parse_error,
                        "chunk " + e.name + " does not decompress: " + ZSTD_getErrorName(size));
    }
    if (size != out.size()) {
      return file_error(StatusCode::parse_error, "chunk " + e.name + " decompresses to " +
                                                     std::to_string(size) + " bytes, expected " +
                                                     std::to_string(out.size()));
    }
  }
  const std::uint64_t checksum = xxh3_64(std::span<const std::byte>(out));
  if (checksum != e.checksum) {
    return file_error(StatusCode::parse_error, "chunk " + e.name + " is corrupt: XXH3 " +
                                                   hex64(checksum) + " differs from the stored " +
                                                   hex64(e.checksum));
  }
  return {};
}

// Name of a table entry: printable ASCII up to the first zero, zeros after it.
std::optional<std::string> decode_name(std::span<const std::byte> bytes) {
  std::string name;
  std::size_t i = 0;
  for (; i < bytes.size() && bytes[i] != std::byte{0}; ++i) {
    const auto c = static_cast<char>(bytes[i]);
    if (c < '!' || c > '~') {
      return std::nullopt;
    }
    name += c;
  }
  for (; i < bytes.size(); ++i) {
    if (bytes[i] != std::byte{0}) {
      return std::nullopt;
    }
  }
  if (name.empty()) {
    return std::nullopt;
  }
  return name;
}

core::Status issues_status(const std::vector<params::ValidationIssue>& issues) {
  std::string text = "world file: invalid metadata:";
  for (const params::ValidationIssue& i : issues) {
    text += "\n" + i.path + ": " + i.message;
  }
  return core::make_status(StatusCode::parse_error, text);
}

core::Provenance decode_provenance(params::detail::ObjectReader& r,
                                   std::vector<params::ValidationIssue>& issues) {
  core::Provenance p;
  if (auto v = r.string("profile")) {
    p.profile = std::move(*v);
  }
  if (auto v = r.uint64("seed")) {
    p.seed = *v;
  }
  if (auto v = r.string("params_hash")) {
    if (auto h = parse_hex64(*v)) {
      p.params_hash = *h;
    } else {
      params::detail::add_error(issues, r.child("params_hash"),
                                "\"" + *v + "\" is not 16 lowercase hex digits");
    }
  }
  if (auto v = r.string("library_version")) {
    if (auto lv = parse_version(*v)) {
      p.library_version = *lv;
    } else {
      params::detail::add_error(issues, r.child("library_version"),
                                "\"" + *v + "\" is not a version major.minor.patch");
    }
  }
  if (auto v = r.integer("attempt", 0, 0xFFFFFFFF)) {
    p.attempt = static_cast<std::uint32_t>(*v);
  }
  if (const Json* a = r.array("completed_stages")) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      if ((*a)[i].is_string()) {
        p.completed_stages.push_back((*a)[i].get<std::string>());
      } else {
        params::detail::add_type_error(issues, json_index_path(r.child("completed_stages"), i),
                                       "string", (*a)[i]);
      }
    }
  }
  if (const Json* a = r.array("rejections")) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      params::detail::ObjectReader e((*a)[i], json_index_path(r.child("rejections"), i), issues);
      if (!e.valid()) {
        continue;
      }
      core::RejectionRecord rec;
      if (auto v = e.integer("attempt", 0, 0xFFFFFFFF)) {
        rec.attempt = static_cast<std::uint32_t>(*v);
      }
      if (auto v = e.string("stage")) {
        rec.stage = std::move(*v);
      }
      if (auto v = e.string("check_id")) {
        rec.check_id = std::move(*v);
      }
      constexpr double kMax = std::numeric_limits<double>::max();
      if (auto v = e.number("measured", -kMax, kMax)) {
        rec.measured = *v;
      }
      if (auto v = e.number("required", -kMax, kMax)) {
        rec.required = *v;
      }
      e.finish();
      p.rejections.push_back(std::move(rec));
    }
  }
  r.finish();
  return p;
}

}  // namespace

core::Expected<std::vector<std::byte>> write_world(const core::WorldState& state,
                                                   const params::GenParams& params,
                                                   const WriteOptions& options) noexcept {
  try {
    std::vector<std::byte> out;
    VectorSink sink(out);
    if (core::Status s = write_to(sink, state, params, options); !s.ok()) {
      return core::make_error(std::move(s));
    }
    return out;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("writing the world failed: ") + e.what());
  }
}

core::Status write_world_file(const std::filesystem::path& path, const core::WorldState& state,
                              const params::GenParams& params,
                              const WriteOptions& options) noexcept {
  try {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    core::Status status;
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) {
        return core::make_status(StatusCode::io_error, "cannot create " + tmp.generic_string());
      }
      FileSink sink(out, tmp.generic_string());
      status = write_to(sink, state, params, options);
      out.close();
      if (status.ok() && !out) {
        status = core::make_status(StatusCode::io_error, "cannot write " + tmp.generic_string());
      }
    }
    std::error_code ec;
    if (status.ok()) {
      std::filesystem::rename(tmp, path, ec);
      if (ec) {
        status = core::make_status(StatusCode::io_error, "cannot rename " + tmp.generic_string() +
                                                             " to " + path.generic_string() + ": " +
                                                             ec.message());
      }
    }
    if (!status.ok()) {
      std::filesystem::remove(tmp, ec);
    }
    return status;
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::io_error,
                             "writing " + path.generic_string() + " failed: " + e.what());
  }
}

WorldReader::WorldReader() noexcept { field_chunk_.fill(-1); }
WorldReader::WorldReader(WorldReader&&) noexcept = default;
WorldReader& WorldReader::operator=(WorldReader&&) noexcept = default;
WorldReader::~WorldReader() = default;

core::Expected<WorldReader> WorldReader::open(ByteSource& source) noexcept {
  try {
    WorldReader reader;
    if (core::Status s = reader.load(source); !s.ok()) {
      return core::make_error(std::move(s));
    }
    return reader;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("opening the world failed: ") + e.what());
  }
}

core::Expected<WorldReader> WorldReader::open_file(const std::filesystem::path& path) noexcept {
  try {
    core::Expected<FileSource> file = FileSource::open(path);
    if (!file) {
      return core::make_error(std::move(file.error()));
    }
    WorldReader reader;
    reader.owned_ = std::make_unique<FileSource>(std::move(*file));
    if (core::Status s = reader.load(*reader.owned_); !s.ok()) {
      s.message = path.generic_string() + ": " + s.message;
      return core::make_error(std::move(s));
    }
    return reader;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            "opening " + path.generic_string() + " failed: " + e.what());
  }
}

core::Expected<WorldReader> WorldReader::open_buffer(std::span<const std::byte> data) noexcept {
  try {
    WorldReader reader;
    reader.owned_ = std::make_unique<MemorySource>(data);
    if (core::Status s = reader.load(*reader.owned_); !s.ok()) {
      return core::make_error(std::move(s));
    }
    return reader;
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("opening the world failed: ") + e.what());
  }
}

core::Status WorldReader::load(ByteSource& source) noexcept {
  try {
    source_ = &source;
    const std::uint64_t size = source.size();
    if (size < kWorldHeaderSize) {
      return file_error(StatusCode::parse_error,
                        std::to_string(size) + " bytes is shorter than the 64-byte header");
    }

    // Header.
    std::array<std::byte, kWorldHeaderSize> hb{};
    if (core::Status s = source.read_at(0, hb); !s.ok()) {
      return s;
    }
    ByteReader r(hb);
    for (char c : kWorldMagic) {
      if (r.u8() != static_cast<std::uint8_t>(c)) {
        return file_error(StatusCode::parse_error, "not a world file (bad magic)");
      }
    }
    header_.format_version = r.u16();
    if (header_.format_version > kWorldFormatVersion) {
      return file_error(StatusCode::unsupported_version,
                        "format version " + std::to_string(header_.format_version) +
                            " is newer than the supported " + std::to_string(kWorldFormatVersion));
    }
    if (header_.format_version == 0) {
      return file_error(StatusCode::parse_error, "format version 0 is invalid");
    }
    header_.flags = r.u16();
    if (header_.flags != 0) {
      return file_error(StatusCode::parse_error,
                        "unknown header flags " + std::to_string(header_.flags));
    }
    header_.grid.n = r.i32();
    header_.grid.dx_m = r.f64();
    header_.grid.hu_m = r.f64();
    if (core::Status s = core::validate(header_.grid); !s.ok()) {
      return file_error(StatusCode::parse_error, "invalid grid: " + s.message);
    }
    header_.seed = r.u64();
    header_.params_hash = r.u64();
    header_.library_version = unpack_version(r.u32());
    header_.chunk_count = r.u32();
    header_.chunk_table_offset = r.u64();
    if (r.u32() != 0) {
      return file_error(StatusCode::parse_error, "nonzero header padding");
    }

    // Chunk table: the last bytes of the file.
    const std::uint64_t table = header_.chunk_table_offset;
    const std::uint64_t table_size = std::uint64_t{header_.chunk_count} * kChunkEntrySize;
    if (table < kWorldHeaderSize || table > size || table_size != size - table) {
      return file_error(StatusCode::parse_error,
                        "chunk table of " + std::to_string(header_.chunk_count) +
                            " entries at offset " + std::to_string(table) + " does not end the " +
                            std::to_string(size) + "-byte file");
    }
    std::vector<std::byte> tb(table_size);
    if (core::Status s = source.read_at(table, tb); !s.ok()) {
      return s;
    }
    ByteReader t(tb);
    std::int32_t metadata = -1;
    const std::int64_t cells = header_.grid.cells();
    for (std::uint32_t i = 0; i < header_.chunk_count; ++i) {
      const std::optional<std::string> name = decode_name(t.bytes(kChunkNameSize));
      const std::string where = "chunk table entry " + std::to_string(i);
      if (!name) {
        return file_error(StatusCode::parse_error, where + " has an invalid name");
      }
      ChunkEntry e{.name = *name};
      e.dtype = t.u8();
      const std::uint8_t codec = t.u8();
      const std::uint16_t reserved = t.u16();
      e.raw_size = t.u64();
      e.stored_size = t.u64();
      e.offset = t.u64();
      e.checksum = t.u64();
      if (codec > static_cast<std::uint8_t>(Codec::zstd)) {
        return file_error(StatusCode::parse_error,
                          "chunk " + e.name + " has unknown codec " + std::to_string(codec));
      }
      e.codec = static_cast<Codec>(codec);
      if (reserved != 0) {
        return file_error(StatusCode::parse_error,
                          "chunk " + e.name + " has nonzero reserved bytes");
      }
      if (e.offset < kWorldHeaderSize || e.offset > table || e.stored_size > table - e.offset) {
        return file_error(StatusCode::parse_error,
                          "chunk " + e.name + " lies outside the data area of the file");
      }
      if (e.codec == Codec::raw && e.stored_size != e.raw_size) {
        return file_error(StatusCode::parse_error,
                          "raw chunk " + e.name + " has different stored and raw sizes");
      }
      for (const ChunkEntry& other : chunks_) {
        if (other.name == e.name) {
          return file_error(StatusCode::parse_error, "chunk " + e.name + " appears twice");
        }
      }
      const auto index = static_cast<std::int32_t>(chunks_.size());
      if (e.name == kMetadataChunkName) {
        if (e.dtype != kMetadataDType) {
          return file_error(
              StatusCode::parse_error,
              "metadata chunk has dtype " + std::to_string(e.dtype) + ", expected 255");
        }
        metadata = index;
      } else if (const std::optional<FieldId> id = core::field_id_from_name(e.name)) {
        const core::FieldInfo& info = core::field_info(*id);
        if (e.dtype != static_cast<std::uint8_t>(info.dtype)) {
          return file_error(StatusCode::parse_error,
                            "field chunk " + e.name + " has dtype " + std::to_string(e.dtype) +
                                ", the registry says " +
                                std::to_string(static_cast<int>(info.dtype)));
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(cells) * core::dtype_size(info.dtype);
        if (e.raw_size != expected) {
          return file_error(StatusCode::parse_error,
                            "field chunk " + e.name + " holds " + std::to_string(e.raw_size) +
                                " bytes, the grid needs " + std::to_string(expected));
        }
        field_chunk_[static_cast<std::size_t>(*id)] = index;
      } else {
        unknown_.push_back(e.name);
      }
      chunks_.push_back(std::move(e));
    }

    // Chunks must not overlap.
    std::vector<std::size_t> order(chunks_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return chunks_[a].offset < chunks_[b].offset ||
             (chunks_[a].offset == chunks_[b].offset && a < b);
    });
    for (std::size_t k = 1; k < order.size(); ++k) {
      const ChunkEntry& prev = chunks_[order[k - 1]];
      if (prev.offset + prev.stored_size > chunks_[order[k]].offset) {
        return file_error(StatusCode::parse_error,
                          "chunks " + prev.name + " and " + chunks_[order[k]].name + " overlap");
      }
    }

    // Metadata.
    if (metadata < 0) {
      return file_error(StatusCode::parse_error, "no metadata chunk");
    }
    const ChunkEntry& me = chunks_[static_cast<std::size_t>(metadata)];
    std::string text(me.raw_size, '\0');
    if (core::Status s = read_chunk(
            source, me,
            std::span<std::byte>(reinterpret_cast<std::byte*>(text.data()), text.size()));
        !s.ok()) {
      return s;
    }
    core::Expected<Json> meta = parse_json(text);
    if (!meta) {
      return file_error(StatusCode::parse_error, "metadata: " + meta.error().message);
    }

    std::vector<params::ValidationIssue> issues;
    params::detail::ObjectReader root(*meta, "$", issues);
    if (!root.valid()) {
      return issues_status(issues);
    }
    if (const auto v = root.integer("schema_version", 1, std::int64_t{1} << 53)) {
      if (*v > kWorldMetadataVersion) {
        return file_error(StatusCode::unsupported_version,
                          "metadata version " + std::to_string(*v) +
                              " is newer than the supported " +
                              std::to_string(kWorldMetadataVersion));
      }
    }
    if (const Json* p = root.object("provenance")) {
      params::detail::ObjectReader pr(*p, root.child("provenance"), issues);
      provenance_ = decode_provenance(pr, issues);
    }
    const Json* params_json = root.object("params");
    const Json* layers_json = root.object("layers");
    root.finish();
    if (params::has_errors(issues)) {
      return issues_status(issues);
    }
    core::Expected<std::string> params_text = dump_json(*params_json);
    if (!params_text) {
      return std::move(params_text.error());
    }
    core::Expected<params::ParamsDocument> doc = params::load_params(*params_text);
    if (!doc) {
      core::Status s = std::move(doc.error());
      if (s.code != StatusCode::unsupported_version) {
        s.code = StatusCode::parse_error;
      }
      s.message = "world file: metadata params: " + s.message;
      return s;
    }
    params_ = doc->params;
    core::Expected<core::VectorLayers> layers = detail::decode_layers(*layers_json, "$.layers");
    if (!layers) {
      return file_error(StatusCode::parse_error, layers.error().message);
    }
    layers_ = std::move(*layers);

    if (provenance_.seed != header_.seed || provenance_.params_hash != header_.params_hash) {
      return file_error(StatusCode::parse_error,
                        "seed or params_hash of the metadata differ from the header");
    }
    return {};
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::out_of_memory,
                             std::string("reading the world failed: ") + e.what());
  }
}

bool WorldReader::has(FieldId id) const noexcept { return chunk(id) != nullptr; }

const ChunkEntry* WorldReader::chunk(FieldId id) const noexcept {
  const auto i = static_cast<std::size_t>(id);
  if (i >= core::kFieldCount || field_chunk_[i] < 0) {
    return nullptr;
  }
  return &chunks_[static_cast<std::size_t>(field_chunk_[i])];
}

core::Status WorldReader::read_field(FieldId id, std::span<std::byte> out) noexcept {
  try {
    const ChunkEntry* e = chunk(id);
    if (e == nullptr) {
      return core::make_status(StatusCode::not_found,
                               "world file has no field " + std::string(core::field_info(id).name));
    }
    if (out.size() != e->raw_size) {
      return core::make_status(StatusCode::invalid_argument,
                               "field " + e->name + " needs " + std::to_string(e->raw_size) +
                                   " bytes, got " + std::to_string(out.size()));
    }
    return read_chunk(*source_, *e, out);
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::out_of_memory,
                             std::string("reading a field failed: ") + e.what());
  }
}

core::Status WorldReader::read_into(core::WorldState& state,
                                    const pipeline::FieldSet* fields) noexcept {
  try {
    if (!(state.grid() == header_.grid)) {
      return core::make_status(StatusCode::invalid_argument,
                               "the state's grid differs from the world file's");
    }
    for (std::size_t i = 0; i < core::kFieldCount; ++i) {
      const auto id = static_cast<FieldId>(i);
      if (!has(id) || (fields != nullptr && !fields->contains(id))) {
        continue;
      }
      core::Expected<std::span<std::byte>> dst = writable_bytes(state, id);
      if (!dst) {
        return std::move(dst.error());
      }
      if (core::Status s = read_field(id, *dst); !s.ok()) {
        return s;
      }
    }
    state.layers() = layers_;
    state.provenance() = provenance_;
    return {};
  } catch (const std::exception& e) {
    return core::make_status(StatusCode::out_of_memory,
                             std::string("reading the world failed: ") + e.what());
  }
}

namespace {

core::Expected<LoadedWorld> load_all(core::Expected<WorldReader>&& reader,
                                     const ReadOptions& options) {
  if (!reader) {
    return core::make_error(std::move(reader.error()));
  }
  core::MemoryResource& memory =
      options.memory != nullptr ? *options.memory : core::default_memory_resource();
  core::Expected<core::WorldState> state = core::WorldState::create(reader->grid(), memory);
  if (!state) {
    return core::make_error(std::move(state.error()));
  }
  if (core::Status s = reader->read_into(*state, options.fields); !s.ok()) {
    return core::make_error(std::move(s));
  }
  return LoadedWorld{.state = std::move(*state), .params = reader->params()};
}

}  // namespace

core::Expected<LoadedWorld> read_world(ByteSource& source, const ReadOptions& options) noexcept {
  try {
    return load_all(WorldReader::open(source), options);
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("reading the world failed: ") + e.what());
  }
}

core::Expected<LoadedWorld> read_world_file(const std::filesystem::path& path,
                                            const ReadOptions& options) noexcept {
  try {
    return load_all(WorldReader::open_file(path), options);
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("reading the world failed: ") + e.what());
  }
}

core::Expected<LoadedWorld> read_world_buffer(std::span<const std::byte> data,
                                              const ReadOptions& options) noexcept {
  try {
    return load_all(WorldReader::open_buffer(data), options);
  } catch (const std::exception& e) {
    return core::make_error(StatusCode::out_of_memory,
                            std::string("reading the world failed: ") + e.what());
  }
}

}  // namespace wg::io
