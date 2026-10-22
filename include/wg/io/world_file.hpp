#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/provenance.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/core/version.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/io/byte_source.hpp"
#include "wg/pipeline/field_set.hpp"

namespace wg::io {

// World file .wgw (plan section 6.5, specified in docs/file_format.md, decisions in ADR 0007):
// a 64-byte header, one independently compressed chunk per raster field, a JSON metadata chunk
// (provenance, the full parameters, all vector layers) and a chunk table at the end. Every
// chunk carries the XXH3-64 of its raw bytes. A snapshot is a world file whose provenance
// names the last completed stage.

/// Format version written by this library; files with a higher version are refused.
inline constexpr std::uint16_t kWorldFormatVersion = 1;
/// Version of the metadata JSON inside the file.
inline constexpr std::int64_t kWorldMetadataVersion = 1;

inline constexpr std::array<char, 4> kWorldMagic = {'W', 'G', 'W', '\0'};
inline constexpr std::size_t kWorldHeaderSize = 64;
inline constexpr std::size_t kChunkEntrySize = 68;
inline constexpr std::size_t kChunkNameSize = 32;
/// Name and dtype byte of the metadata chunk; field chunks use the field name and DType.
inline constexpr std::string_view kMetadataChunkName = "$metadata";
inline constexpr std::uint8_t kMetadataDType = 255;

/// Compression of a chunk.
enum class Codec : std::uint8_t { raw = 0, zstd = 1 };

/// Fixed-size header at offset 0.
struct WorldHeader {
  std::uint16_t format_version = kWorldFormatVersion;
  std::uint16_t flags = 0;  ///< reserved, 0 in version 1
  core::Grid grid;
  std::uint64_t seed = 0;
  std::uint64_t params_hash = 0;
  /// Version of the library that wrote the file (the generator's is in the provenance).
  core::Version library_version{};
  std::uint32_t chunk_count = 0;
  std::uint64_t chunk_table_offset = 0;

  friend bool operator==(const WorldHeader&, const WorldHeader&) = default;
};

/// One entry of the chunk table.
struct ChunkEntry {
  std::string name;        ///< field name, or kMetadataChunkName
  std::uint8_t dtype = 0;  ///< DType value, or kMetadataDType
  Codec codec = Codec::raw;
  std::uint64_t raw_size = 0;
  std::uint64_t stored_size = 0;
  std::uint64_t offset = 0;
  std::uint64_t checksum = 0;  ///< XXH3-64 of the raw (decompressed) bytes

  friend bool operator==(const ChunkEntry&, const ChunkEntry&) = default;
};

struct WriteOptions {
  /// Codec of every chunk. A chunk zstd does not make smaller is stored raw.
  Codec codec = Codec::zstd;
  /// zstd compression level, 1 to 19. A fixed level keeps the file bytes reproducible.
  int zstd_level = 3;
};

/// A world file in memory: every field present in the state, the vector layers, the provenance
/// and params. Errors: invalid_argument if params_hash(params) differs from the provenance's
/// params_hash, if a layer number is not finite (JSON has no NaN or infinity), the river graph
/// is inconsistent or the options are out of range; out_of_memory; internal if zstd fails.
[[nodiscard]] core::Expected<std::vector<std::byte>> write_world(
    const core::WorldState& state, const params::GenParams& params,
    const WriteOptions& options = {}) noexcept;

/// write_world streamed to a file: written to "<path>.tmp", then renamed over path, so a
/// reader never sees a half-written world. Errors as write_world, plus io_error.
[[nodiscard]] core::Status write_world_file(const std::filesystem::path& path,
                                            const core::WorldState& state,
                                            const params::GenParams& params,
                                            const WriteOptions& options = {}) noexcept;

/// Reads a world file lazily. open() reads and checks the header, the chunk table and the
/// metadata chunk only; read_field() then reads, decompresses and verifies exactly one field
/// chunk, so a partial read touches nothing else.
///
/// Refused by open: unsupported_version for a format or metadata version newer than this
/// library's; parse_error for a bad magic, nonzero reserved bytes or flags, a grid that
/// validate() rejects, a table or chunk outside the file or overlapping another, a known field
/// with the wrong dtype or size, a duplicate chunk, a missing or corrupt metadata chunk (XXH3
/// mismatch) or metadata that does not match the header. Chunks named after fields this
/// library does not know (appended by a newer registry) are skipped and listed.
class WorldReader {
 public:
  /// Reader over a caller-owned source that must outlive it.
  [[nodiscard]] static core::Expected<WorldReader> open(ByteSource& source) noexcept;
  /// Reader that owns a FileSource of path. Errors: io_error if it cannot be opened.
  [[nodiscard]] static core::Expected<WorldReader> open_file(
      const std::filesystem::path& path) noexcept;
  /// Reader over a caller-owned buffer that must outlive it.
  [[nodiscard]] static core::Expected<WorldReader> open_buffer(
      std::span<const std::byte> data) noexcept;

  WorldReader(const WorldReader&) = delete;
  WorldReader& operator=(const WorldReader&) = delete;
  WorldReader(WorldReader&&) noexcept;
  WorldReader& operator=(WorldReader&&) noexcept;
  ~WorldReader();

  [[nodiscard]] const WorldHeader& header() const noexcept { return header_; }
  [[nodiscard]] const core::Grid& grid() const noexcept { return header_.grid; }
  /// Every table entry in file order, unknown and metadata chunks included.
  [[nodiscard]] const std::vector<ChunkEntry>& chunks() const noexcept { return chunks_; }
  [[nodiscard]] const core::Provenance& provenance() const noexcept { return provenance_; }
  [[nodiscard]] const params::GenParams& params() const noexcept { return params_; }
  [[nodiscard]] const core::VectorLayers& layers() const noexcept { return layers_; }
  /// Names of chunks this library does not know; they are never read.
  [[nodiscard]] const std::vector<std::string>& unknown_chunks() const noexcept { return unknown_; }

  [[nodiscard]] bool has(core::FieldId id) const noexcept;
  /// Table entry of field id, or nullptr if the file does not hold it.
  [[nodiscard]] const ChunkEntry* chunk(core::FieldId id) const noexcept;

  /// Reads field id into out, which must hold exactly its raw size (cells times the dtype
  /// size). Errors: not_found if absent, invalid_argument for a wrong size, parse_error naming
  /// the chunk if it does not decompress to its raw size or its XXH3 differs, io_error.
  [[nodiscard]] core::Status read_field(core::FieldId id, std::span<std::byte> out) noexcept;

  /// Reads the fields in fields (every field of the file if nullptr; requested fields the file
  /// lacks are skipped) into state, and replaces its layers and provenance. Errors:
  /// invalid_argument if the state's grid differs from the file's, then as read_field and
  /// WorldState::write.
  [[nodiscard]] core::Status read_into(core::WorldState& state,
                                       const pipeline::FieldSet* fields = nullptr) noexcept;

 private:
  WorldReader() noexcept;

  core::Status load(ByteSource& source) noexcept;

  std::unique_ptr<ByteSource> owned_;
  ByteSource* source_ = nullptr;
  WorldHeader header_;
  std::vector<ChunkEntry> chunks_;
  std::array<std::int32_t, core::kFieldCount> field_chunk_{};  ///< index into chunks_, or -1
  std::vector<std::string> unknown_;
  core::Provenance provenance_;
  params::GenParams params_;
  core::VectorLayers layers_;
};

/// A world read back into memory.
struct LoadedWorld {
  core::WorldState state;
  params::GenParams params;
};

struct ReadOptions {
  /// Fields to load (a partial read); nullptr loads every field of the file.
  const pipeline::FieldSet* fields = nullptr;
  /// Source of the field storage; nullptr for core::default_memory_resource().
  core::MemoryResource* memory = nullptr;
};

/// Opens a world and loads its fields into a new WorldState. Errors as WorldReader::open and
/// read_into, plus out_of_memory.
[[nodiscard]] core::Expected<LoadedWorld> read_world(ByteSource& source,
                                                     const ReadOptions& options = {}) noexcept;
[[nodiscard]] core::Expected<LoadedWorld> read_world_file(const std::filesystem::path& path,
                                                          const ReadOptions& options = {}) noexcept;
[[nodiscard]] core::Expected<LoadedWorld> read_world_buffer(
    std::span<const std::byte> data, const ReadOptions& options = {}) noexcept;

}  // namespace wg::io
