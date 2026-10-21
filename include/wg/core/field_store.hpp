#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/field_ids.hpp"

namespace wg::core {

/// Byte-level storage of the raster fields of one world: one slot per FieldId, each empty,
/// owned (allocated from a MemoryResource, 64-byte aligned, zero-filled) or adopted (a
/// caller-owned buffer used without copying). Every field holds cells values of its
/// registered dtype. Typed access goes through WorldState.
class FieldStore {
 public:
  /// Alignment of owned buffers, enough for any SIMD width the kernels use.
  static constexpr std::size_t kAlignment = 64;

  /// Store for fields of cells values each; resource must outlive the store.
  explicit FieldStore(std::int64_t cells,
                      MemoryResource& resource = default_memory_resource()) noexcept;
  ~FieldStore();

  FieldStore(const FieldStore&) = delete;
  FieldStore& operator=(const FieldStore&) = delete;
  FieldStore(FieldStore&& other) noexcept;
  FieldStore& operator=(FieldStore&& other) noexcept;

  /// Size of field id in bytes: cells times the size of its dtype.
  [[nodiscard]] std::size_t field_bytes(FieldId id) const noexcept;

  [[nodiscard]] bool has(FieldId id) const noexcept;
  [[nodiscard]] bool is_adopted(FieldId id) const noexcept;

  /// Storage of a present field, or not_found.
  [[nodiscard]] Expected<std::span<const std::byte>> get(FieldId id) const noexcept;

  /// Storage of a field, allocated and zero-filled if absent; out_of_memory on failure.
  [[nodiscard]] Expected<std::span<std::byte>> ensure(FieldId id) noexcept;

  /// Uses external as the storage of field id without copying, replacing (and freeing) any
  /// previous storage. external must hold exactly field_bytes(id) bytes aligned to the dtype
  /// and outlive the store or a later release(id); otherwise invalid_argument.
  Status adopt(FieldId id, std::span<std::byte> external) noexcept;

  /// Drops field id: frees owned storage, forgets adopted storage. No-op if absent.
  void release(FieldId id) noexcept;

  /// Releases every field.
  void clear() noexcept;

  /// Bytes currently allocated from the resource (adopted buffers not included).
  [[nodiscard]] std::size_t bytes_owned() const noexcept { return bytes_owned_; }

  [[nodiscard]] std::int64_t cells() const noexcept { return cells_; }

 private:
  struct Slot {
    std::byte* data = nullptr;
    std::size_t bytes = 0;
    bool owned = false;
  };

  [[nodiscard]] Slot& slot(FieldId id) noexcept;
  [[nodiscard]] const Slot& slot(FieldId id) const noexcept;

  std::int64_t cells_;
  MemoryResource* resource_;
  std::array<Slot, kFieldCount> slots_{};
  std::size_t bytes_owned_ = 0;
};

}  // namespace wg::core
