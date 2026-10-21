#include "wg/params/params.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "wg/core/assert.hpp"

namespace wg::params {

// Enumerations are stored as enum classes over std::uint8_t and accessed through that
// character type, which may alias any object.
static_assert(std::is_same_v<std::uint8_t, unsigned char>);

namespace {

void* element_ptr(GenParams& params, const ParamDescriptor& d, std::size_t element) noexcept {
  WG_ASSERT(element < (d.count == 0 ? 1 : d.count), "parameter element out of range");
  void* base = d.member(params);
  switch (d.kind) {
    case ParamKind::real:
      return static_cast<double*>(base) + element;
    case ParamKind::integer:
      return static_cast<std::int32_t*>(base) + element;
    case ParamKind::boolean:
      return static_cast<bool*>(base) + element;
    case ParamKind::enumeration:
      return static_cast<std::uint8_t*>(base) + element;
  }
  return base;
}

}  // namespace

double get_param(const GenParams& params, const ParamDescriptor& d, std::size_t element) noexcept {
  // The accessor takes a mutable GenParams; reading through it does not modify the object.
  void* p = element_ptr(const_cast<GenParams&>(params), d, element);
  switch (d.kind) {
    case ParamKind::real:
      return *static_cast<const double*>(p);
    case ParamKind::integer:
      return static_cast<double>(*static_cast<const std::int32_t*>(p));
    case ParamKind::boolean:
      return *static_cast<const bool*>(p) ? 1.0 : 0.0;
    case ParamKind::enumeration:
      return static_cast<double>(*static_cast<const std::uint8_t*>(p));
  }
  return 0.0;
}

void set_param(GenParams& params, const ParamDescriptor& d, std::size_t element,
               double value) noexcept {
  void* p = element_ptr(params, d, element);
  switch (d.kind) {
    case ParamKind::real:
      *static_cast<double*>(p) = value;
      return;
    case ParamKind::integer:
      *static_cast<std::int32_t*>(p) = static_cast<std::int32_t>(value);
      return;
    case ParamKind::boolean:
      *static_cast<bool*>(p) = value != 0.0;
      return;
    case ParamKind::enumeration:
      *static_cast<std::uint8_t*>(p) = static_cast<std::uint8_t>(value);
      return;
  }
}

std::string_view param_label(const GenParams& params, const ParamDescriptor& d) noexcept {
  if (d.kind != ParamKind::enumeration) {
    return {};
  }
  const auto index = static_cast<std::size_t>(get_param(params, d));
  return index < d.labels.size() ? d.labels[index] : std::string_view{};
}

}  // namespace wg::params
