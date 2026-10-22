#include "io/binary.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

using wg::io::detail::ByteReader;
using wg::io::detail::ByteWriter;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> values) {
  std::vector<std::byte> b;
  b.reserve(values.size());
  for (int v : values) {
    b.push_back(static_cast<std::byte>(v));
  }
  return b;
}

}  // namespace

TEST_CASE("io.binary writer packs little-endian values") {
  std::vector<std::byte> out;
  ByteWriter w(out);
  w.u8(0xAB);
  w.u16(0x1234);
  w.u32(0xDEADBEEF);
  w.i32(-2);
  w.u64(0x0102030405060708ULL);
  w.f64(1.0);
  w.zeros(2);
  const std::array<std::byte, 2> tail = {std::byte{7}, std::byte{9}};
  w.bytes(tail);
  CHECK(out == bytes({0xAB, 0x34, 0x12, 0xEF, 0xBE, 0xAD, 0xDE, 0xFE, 0xFF, 0xFF, 0xFF,
                      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0xF0, 0x3F, 0x00, 0x00, 7,    9}));
}

TEST_CASE("io.binary reader reads what the writer wrote") {
  std::vector<std::byte> out;
  ByteWriter w(out);
  w.u16(65535);
  w.i32(-123456);
  w.u64(0xFEEDFACECAFEBEEFULL);
  w.f64(-0.1);
  ByteReader r(out);
  CHECK(r.u16() == 65535);
  CHECK(r.i32() == -123456);
  CHECK(r.u64() == 0xFEEDFACECAFEBEEFULL);
  CHECK(r.f64() == -0.1);
  CHECK(r.position() == out.size());
  CHECK_FALSE(r.failed());
}

TEST_CASE("io.binary reading past the end fails and yields zeros") {
  const std::vector<std::byte> in = bytes({1, 2, 3});
  ByteReader r(in);
  CHECK(r.u16() == 0x0201);
  CHECK(r.u32() == 0);
  CHECK(r.failed());
  CHECK(r.position() == in.size());
  CHECK(r.bytes(1).empty());
}
