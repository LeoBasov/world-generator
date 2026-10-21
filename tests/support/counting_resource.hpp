#pragma once

#include <cstddef>

#include "wg/core/memory.hpp"

namespace wg::test {

/// MemoryResource that counts allocations and outstanding bytes, and can be told to fail.
class CountingResource final : public wg::core::MemoryResource {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    if (fail) {
      return nullptr;
    }
    void* p = wg::core::default_memory_resource().allocate(bytes, alignment);
    if (p != nullptr) {
      ++allocations;
      bytes_outstanding += bytes;
      last_alignment = alignment;
    }
    return p;
  }

  void deallocate(void* p, std::size_t bytes, std::size_t alignment) noexcept override {
    ++deallocations;
    bytes_outstanding -= bytes;
    wg::core::default_memory_resource().deallocate(p, bytes, alignment);
  }

  bool fail = false;
  std::size_t allocations = 0;
  std::size_t deallocations = 0;
  std::size_t bytes_outstanding = 0;
  std::size_t last_alignment = 0;
};

}  // namespace wg::test
