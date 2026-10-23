#include "world_hash.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include "wg/core/dtype.hpp"
#include "wg/io/export.hpp"
#include "wg/io/hash.hpp"

namespace wg::cli {

namespace {

std::string hex16(std::uint64_t value) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string text(16, '0');
  for (int i = 15; i >= 0; --i) {
    text[static_cast<std::size_t>(i)] = kDigits[value & 0xFU];
    value >>= 4U;
  }
  return text;
}

template <class T>
core::Expected<std::span<const std::byte>> field_bytes(const core::WorldState& state,
                                                       core::FieldId id) {
  auto view = state.read<T>(id);
  if (!view) {
    return core::make_error(std::move(view.error()));
  }
  return std::as_bytes(view->data);
}

core::Expected<std::span<const std::byte>> raw_bytes(const core::WorldState& state,
                                                     core::FieldId id) {
  switch (core::field_info(id).dtype) {
    case core::DType::u8:
      return field_bytes<std::uint8_t>(state, id);
    case core::DType::u16:
      return field_bytes<std::uint16_t>(state, id);
    case core::DType::i32:
      return field_bytes<std::int32_t>(state, id);
    case core::DType::f32:
      return field_bytes<float>(state, id);
  }
  return core::make_error(core::StatusCode::internal, "unknown dtype");
}

/// Combines the field hashes and the layers into the world hash.
core::Expected<WorldHashes> finish(std::int32_t n, WorldHashes hashes,
                                   const core::VectorLayers& layers) {
  auto json = io::layers_to_json(layers);
  if (!json) {
    return core::make_error(std::move(json.error()));
  }
  hashes.layers = io::xxh3_64(std::string_view(*json));
  std::string text = "grid " + std::to_string(n) + "\n";
  for (const auto& [id, hash] : hashes.fields) {
    text += std::string(core::field_info(id).name) + " " + hex16(hash) + "\n";
  }
  text += "layers " + hex16(hashes.layers) + "\n";
  hashes.world = io::xxh3_64(std::string_view(text));
  return hashes;
}

}  // namespace

std::string hex64(std::uint64_t value) { return "0x" + hex16(value); }

core::Expected<WorldHashes> hash_world(const core::WorldState& state) {
  WorldHashes hashes;
  for (std::size_t i = 0; i < core::kFieldCount; ++i) {
    const auto id = static_cast<core::FieldId>(i);
    if (!state.has(id)) {
      continue;
    }
    auto bytes = raw_bytes(state, id);
    if (!bytes) {
      return core::make_error(std::move(bytes.error()));
    }
    hashes.fields.emplace_back(id, io::xxh3_64(*bytes));
  }
  return finish(state.grid().n, std::move(hashes), state.layers());
}

core::Expected<WorldHashes> hash_world_file(io::WorldReader& reader) {
  WorldHashes hashes;
  std::vector<std::byte> buffer;
  for (std::size_t i = 0; i < core::kFieldCount; ++i) {
    const auto id = static_cast<core::FieldId>(i);
    const io::ChunkEntry* chunk = reader.chunk(id);
    if (chunk == nullptr) {
      continue;
    }
    buffer.resize(static_cast<std::size_t>(chunk->raw_size));
    if (core::Status s = reader.read_field(id, buffer); !s.ok()) {
      return core::make_error(std::move(s));
    }
    hashes.fields.emplace_back(id, io::xxh3_64(buffer));
  }
  return finish(reader.grid().n, std::move(hashes), reader.layers());
}

core::Status StageHasher::on_stage(const core::WorldState& state,
                                   const pipeline::SnapshotInfo& info) noexcept {
  try {
    auto hashes = hash_world(state);
    if (!hashes) {
      return std::move(hashes.error());
    }
    hashes_.push_back(StageHash{
        .stage = std::string(info.stage), .attempt = info.attempt, .world = hashes->world});
    return {};
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "stage hash");
  }
}

std::vector<StageHash> StageHasher::stages_of(std::uint32_t attempt) const {
  std::vector<StageHash> out;
  for (const StageHash& h : hashes_) {
    if (h.attempt == attempt) {
      out.push_back(h);
    }
  }
  return out;
}

}  // namespace wg::cli
