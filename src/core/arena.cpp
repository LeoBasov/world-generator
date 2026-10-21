#include "wg/core/arena.hpp"

#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "wg/core/assert.hpp"

namespace wg::core {

Expected<Arena> Arena::create(std::size_t capacity_bytes, MemoryResource& resource) noexcept {
  if (capacity_bytes == 0) {
    return make_error(StatusCode::invalid_argument, "arena capacity must be positive");
  }
  void* p = resource.allocate(capacity_bytes, kAlignment);
  if (p == nullptr) {
    const std::string message = "cannot get " + std::to_string(capacity_bytes) + " bytes";
    return make_error(StatusCode::out_of_memory, message);
  }
  return Arena(static_cast<std::byte*>(p), capacity_bytes, resource);
}

Arena::Arena(std::byte* base, std::size_t capacity, MemoryResource& resource) noexcept
    : resource_(&resource), base_(base), capacity_(capacity) {}

Arena::~Arena() {
  if (base_ != nullptr) {
    resource_->deallocate(base_, capacity_, kAlignment);
  }
}

Arena::Arena(Arena&& other) noexcept
    : resource_(other.resource_),
      base_(std::exchange(other.base_, nullptr)),
      capacity_(std::exchange(other.capacity_, 0)),
      offset_(std::exchange(other.offset_, 0)) {}

Arena& Arena::operator=(Arena&& other) noexcept {
  if (this != &other) {
    if (base_ != nullptr) {
      resource_->deallocate(base_, capacity_, kAlignment);
    }
    resource_ = other.resource_;
    base_ = std::exchange(other.base_, nullptr);
    capacity_ = std::exchange(other.capacity_, 0);
    offset_ = std::exchange(other.offset_, 0);
  }
  return *this;
}

void Arena::rewind(Marker m) noexcept {
  WG_ASSERT(m.offset <= offset_, "arena marker lies beyond the current position");
  offset_ = m.offset;
}

Expected<std::span<std::byte>> Arena::bump(std::size_t count, std::size_t size) noexcept {
  // Round the start up to kAlignment; offset_ never exceeds capacity_, so this cannot overflow
  // for any capacity an allocator can return.
  const std::size_t start = (offset_ + kAlignment - 1) / kAlignment * kAlignment;
  const std::size_t available = start <= capacity_ ? capacity_ - start : 0;
  if (count > std::numeric_limits<std::size_t>::max() / size || count * size > available) {
    std::string message = "arena of " + std::to_string(capacity_) + " bytes, ";
    message += std::to_string(offset_) + " used, cannot hold " + std::to_string(count);
    message += " values of " + std::to_string(size) + " bytes";
    return make_error(StatusCode::out_of_memory, message);
  }
  const std::size_t bytes = count * size;
  std::byte* p = base_ + start;
  std::memset(p, 0, bytes);
  offset_ = start + bytes;
  return std::span<std::byte>(p, bytes);
}

}  // namespace wg::core
