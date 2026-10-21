#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "wg/generated/field_ids.hpp"

namespace wg::core {

/// Storage type of a C++ value type; the inverse of the generated dtype_traits.
template <class T>
struct dtype_of;
template <>
struct dtype_of<std::uint8_t> : std::integral_constant<DType, DType::u8> {};
template <>
struct dtype_of<std::uint16_t> : std::integral_constant<DType, DType::u16> {};
template <>
struct dtype_of<std::int32_t> : std::integral_constant<DType, DType::i32> {};
template <>
struct dtype_of<float> : std::integral_constant<DType, DType::f32> {};

/// A type a raster field can hold (cv-qualifiers ignored).
template <class T>
concept FieldValue = requires { dtype_of<std::remove_cv_t<T>>::value; };

template <FieldValue T>
inline constexpr DType dtype_of_v = dtype_of<std::remove_cv_t<T>>::value;

/// Size of one value of a dtype in bytes.
constexpr std::size_t dtype_size(DType d) noexcept {
  switch (d) {
    case DType::u8:
      return 1;
    case DType::u16:
      return 2;
    case DType::i32:
    case DType::f32:
      return 4;
  }
  return 0;
}

}  // namespace wg::core
