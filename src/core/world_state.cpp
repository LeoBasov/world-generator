#include "wg/core/world_state.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "wg/core/version.hpp"

namespace wg::core {

namespace {

std::string_view dtype_name(DType d) noexcept {
  switch (d) {
    case DType::u8:
      return "u8";
    case DType::u16:
      return "u16";
    case DType::i32:
      return "i32";
    case DType::f32:
      return "f32";
  }
  return "unknown";
}

}  // namespace

Expected<WorldState> WorldState::create(const Grid& grid, MemoryResource& resource) noexcept {
  if (Status s = validate(grid); !s.ok()) {
    return make_error(std::move(s));
  }
  WorldState state(grid, resource);
  state.provenance_.library_version = version();
  return state;
}

WorldState::WorldState(const Grid& grid, MemoryResource& resource) noexcept
    : grid_(grid), store_(grid.cells(), resource) {}

Status WorldState::check_dtype(FieldId id, DType requested) noexcept {
  const FieldInfo& info = field_info(id);
  if (info.dtype == requested) {
    return {};
  }
  std::string message = "field '" + std::string(info.name) + "' has dtype ";
  message += dtype_name(info.dtype);
  message += ", requested ";
  message += dtype_name(requested);
  return make_status(StatusCode::type_mismatch, message);
}

}  // namespace wg::core
