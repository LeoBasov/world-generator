#pragma once

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

#include "wg/core/expected.hpp"
#include "wg/core/memory.hpp"

namespace wg::core {

/// Scratch memory for kernels (plan section 6.3: no allocation inside per-cell loops). A
/// fixed-capacity bump allocator: one upstream allocation at create, never grows, frees
/// everything at once with rewind or reset. Not thread-safe; use one arena per worker.
class Arena {
 public:
  /// Alignment of every allocation.
  static constexpr std::size_t kAlignment = 64;

  /// Position to rewind to.
  struct Marker {
    std::size_t offset = 0;
  };

  /// Arena of capacity_bytes (> 0) from resource, which must outlive it.
  [[nodiscard]] static Expected<Arena> create(
      std::size_t capacity_bytes, MemoryResource& resource = default_memory_resource()) noexcept;

  ~Arena();
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&& other) noexcept;
  Arena& operator=(Arena&& other) noexcept;

  /// count zero-initialised values of T, 64-byte aligned; out_of_memory if the remaining
  /// capacity is too small. Valid until rewound past or reset.
  template <class T>
  [[nodiscard]] Expected<std::span<T>> allocate(std::size_t count) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "arena values must be trivially copyable");
    static_assert(std::is_trivially_default_constructible_v<T>,
                  "arena values must be trivially default constructible");
    static_assert(alignof(T) <= kAlignment, "arena values must not be over-aligned");
    Expected<std::span<std::byte>> bytes = bump(count, sizeof(T));
    if (!bytes) {
      return make_error(std::move(bytes.error()));
    }
    return std::span<T>(reinterpret_cast<T*>(bytes->data()), count);
  }

  [[nodiscard]] Marker mark() const noexcept { return Marker{offset_}; }

  /// Frees everything allocated after marker m was taken.
  void rewind(Marker m) noexcept;

  /// Frees everything.
  void reset() noexcept { offset_ = 0; }

  [[nodiscard]] std::size_t used() const noexcept { return offset_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

 private:
  Arena(std::byte* base, std::size_t capacity, MemoryResource& resource) noexcept;

  /// Zeroed, aligned storage for count values of size bytes each.
  [[nodiscard]] Expected<std::span<std::byte>> bump(std::size_t count, std::size_t size) noexcept;

  MemoryResource* resource_;
  std::byte* base_;
  std::size_t capacity_;
  std::size_t offset_ = 0;
};

/// Rewinds an arena to where it was when the scope began.
class ArenaScope {
 public:
  explicit ArenaScope(Arena& arena) noexcept : arena_(&arena), marker_(arena.mark()) {}
  ~ArenaScope() { arena_->rewind(marker_); }
  ArenaScope(const ArenaScope&) = delete;
  ArenaScope& operator=(const ArenaScope&) = delete;
  ArenaScope(ArenaScope&&) = delete;
  ArenaScope& operator=(ArenaScope&&) = delete;

 private:
  Arena* arena_;
  Arena::Marker marker_;
};

}  // namespace wg::core
