#include "wg/core/memory.hpp"

#include <new>

namespace wg::core {

namespace {

class NewDeleteResource final : public MemoryResource {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    return ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);
  }
  void deallocate(void* p, std::size_t /*bytes*/, std::size_t alignment) noexcept override {
    ::operator delete(p, std::align_val_t{alignment});
  }
};

}  // namespace

MemoryResource& default_memory_resource() noexcept {
  static NewDeleteResource resource;
  return resource;
}

}  // namespace wg::core
