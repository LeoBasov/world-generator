#pragma once

#include <cstddef>

namespace wg::core {

/// Source of the large buffers of the library (field storage, arenas). The CLI and tests use
/// the default resource; tests count allocations through their own; Phase 2 may route it to
/// the engine allocator. Implementations must be thread-safe if shared between WorldStates.
/// (A plain interface rather than std::pmr::memory_resource, which Apple's libc++ only
/// provides from macOS 14.)
class MemoryResource {
 public:
  virtual ~MemoryResource() = default;

  /// Returns bytes of storage aligned to alignment (a power of two), or nullptr on failure.
  virtual void* allocate(std::size_t bytes, std::size_t alignment) noexcept = 0;

  /// Returns storage obtained from allocate with the same bytes and alignment.
  virtual void deallocate(void* p, std::size_t bytes, std::size_t alignment) noexcept = 0;

 protected:
  MemoryResource() = default;
  MemoryResource(const MemoryResource&) = default;
  MemoryResource& operator=(const MemoryResource&) = default;
  MemoryResource(MemoryResource&&) = default;
  MemoryResource& operator=(MemoryResource&&) = default;
};

/// Process-wide resource backed by aligned operator new; thread-safe.
MemoryResource& default_memory_resource() noexcept;

}  // namespace wg::core
