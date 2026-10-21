#include "wg/core/memory.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

TEST_CASE("core.memory default resource returns aligned, writable storage") {
  wg::core::MemoryResource& r = wg::core::default_memory_resource();
  CHECK(&r == &wg::core::default_memory_resource());
  for (const std::size_t alignment : {8U, 64U, 256U}) {
    void* p = r.allocate(1000, alignment);
    REQUIRE(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % alignment == 0);
    std::memset(p, 0xAB, 1000);
    r.deallocate(p, 1000, alignment);
  }
}
