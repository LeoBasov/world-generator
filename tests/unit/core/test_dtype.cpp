#include "wg/core/dtype.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

using wg::core::DType;
using wg::core::dtype_of_v;
using wg::core::dtype_size;
using wg::core::FieldValue;

namespace {

static_assert(dtype_of_v<std::uint8_t> == DType::u8);
static_assert(dtype_of_v<std::uint16_t> == DType::u16);
static_assert(dtype_of_v<std::int32_t> == DType::i32);
static_assert(dtype_of_v<float> == DType::f32);
static_assert(dtype_of_v<const float> == DType::f32);
static_assert(FieldValue<float> && FieldValue<const std::uint8_t>);
static_assert(!FieldValue<double> && !FieldValue<std::int64_t> && !FieldValue<std::byte>);

}  // namespace

TEST_CASE("core.dtype sizes match the value types") {
  CHECK(dtype_size(DType::u8) == sizeof(std::uint8_t));
  CHECK(dtype_size(DType::u16) == sizeof(std::uint16_t));
  CHECK(dtype_size(DType::i32) == sizeof(std::int32_t));
  CHECK(dtype_size(DType::f32) == sizeof(float));
  CHECK(dtype_size(static_cast<DType>(255)) == 0);
}

TEST_CASE("core.dtype round trips through the registry traits") {
  for (const auto& info : wg::core::kFields) {
    CHECK(dtype_size(info.dtype) > 0);
  }
  static_assert(dtype_of_v<wg::core::dtype_traits<DType::i32>::type> == DType::i32);
  static_assert(dtype_of_v<wg::core::field_t<wg::core::FieldId::biome>> == DType::u16);
}
