#include "wg/core/field_store.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "wg/core/dtype.hpp"

namespace wg::core {

namespace {

// "field 'name' " followed by detail.
std::string field_message(FieldId id, std::string_view detail) {
  std::string message = "field '";
  message += field_info(id).name;
  message += "' ";
  message += detail;
  return message;
}

}  // namespace

FieldStore::FieldStore(std::int64_t cells, MemoryResource& resource) noexcept
    : cells_(cells), resource_(&resource) {}

FieldStore::~FieldStore() { clear(); }

FieldStore::FieldStore(FieldStore&& other) noexcept
    : cells_(other.cells_),
      resource_(other.resource_),
      slots_(std::exchange(other.slots_, {})),
      bytes_owned_(std::exchange(other.bytes_owned_, 0)) {}

FieldStore& FieldStore::operator=(FieldStore&& other) noexcept {
  if (this != &other) {
    clear();
    cells_ = other.cells_;
    resource_ = other.resource_;
    slots_ = std::exchange(other.slots_, {});
    bytes_owned_ = std::exchange(other.bytes_owned_, 0);
  }
  return *this;
}

std::size_t FieldStore::field_bytes(FieldId id) const noexcept {
  return static_cast<std::size_t>(cells_) * dtype_size(field_info(id).dtype);
}

bool FieldStore::has(FieldId id) const noexcept { return slot(id).data != nullptr; }

bool FieldStore::is_adopted(FieldId id) const noexcept {
  const Slot& s = slot(id);
  return s.data != nullptr && !s.owned;
}

Expected<std::span<const std::byte>> FieldStore::get(FieldId id) const noexcept {
  const Slot& s = slot(id);
  if (s.data == nullptr) {
    return make_error(StatusCode::not_found, field_message(id, "is absent"));
  }
  return std::span<const std::byte>(s.data, s.bytes);
}

Expected<std::span<std::byte>> FieldStore::ensure(FieldId id) noexcept {
  Slot& s = slot(id);
  if (s.data != nullptr) {
    return std::span<std::byte>(s.data, s.bytes);
  }
  const std::size_t bytes = field_bytes(id);
  void* p = resource_->allocate(bytes, kAlignment);
  if (p == nullptr) {
    const std::string detail = "cannot get " + std::to_string(bytes) + " bytes";
    return make_error(StatusCode::out_of_memory, field_message(id, detail));
  }
  std::memset(p, 0, bytes);
  s = Slot{.data = static_cast<std::byte*>(p), .bytes = bytes, .owned = true};
  bytes_owned_ += bytes;
  return std::span<std::byte>(s.data, s.bytes);
}

Status FieldStore::adopt(FieldId id, std::span<std::byte> external) noexcept {
  const std::size_t bytes = field_bytes(id);
  if (external.data() == nullptr || external.size() != bytes) {
    std::string detail = "got a buffer of " + std::to_string(external.size()) + " bytes, ";
    detail += "expected " + std::to_string(bytes);
    return make_status(StatusCode::invalid_argument, field_message(id, detail));
  }
  const auto address = reinterpret_cast<std::uintptr_t>(external.data());
  if (address % dtype_size(field_info(id).dtype) != 0) {
    return make_status(StatusCode::invalid_argument, field_message(id, "got a misaligned buffer"));
  }
  release(id);
  slot(id) = Slot{.data = external.data(), .bytes = bytes, .owned = false};
  return {};
}

void FieldStore::release(FieldId id) noexcept {
  Slot& s = slot(id);
  if (s.owned) {
    resource_->deallocate(s.data, s.bytes, kAlignment);
    bytes_owned_ -= s.bytes;
  }
  s = Slot{};
}

void FieldStore::clear() noexcept {
  for (const FieldInfo& info : kFields) {
    release(info.id);
  }
}

FieldStore::Slot& FieldStore::slot(FieldId id) noexcept {
  return slots_[static_cast<std::size_t>(id)];
}

const FieldStore::Slot& FieldStore::slot(FieldId id) const noexcept {
  return slots_[static_cast<std::size_t>(id)];
}

}  // namespace wg::core
