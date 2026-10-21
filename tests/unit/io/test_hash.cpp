#include "wg/io/hash.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

// Test buffer of xxHash's tests/sanity_test.c (fillTestBuffer).
std::vector<std::byte> sanity_buffer(std::size_t length) {
  std::vector<std::byte> buffer(length);
  std::uint64_t gen = 2654435761U;  // PRIME32
  for (std::byte& b : buffer) {
    b = static_cast<std::byte>(gen >> 56);
    gen *= 11400714785074694797ULL;  // PRIME64
  }
  return buffer;
}

}  // namespace

TEST_CASE("io.hash XXH3-64 matches the xxHash sanity vectors") {
  struct Vector {
    std::size_t length;
    std::uint64_t hash;
  };
  // XSUM_XXH3_testdata of xxHash 0.8.3, seed 0; covers the short, mid and long code paths.
  const std::vector<Vector> vectors = {
      {0, 0x2D06800538D394C2ULL},   {1, 0xC44BDFF4074EECDBULL},    {3, 0x54247382A8D6B94DULL},
      {17, 0x796F5ACD3A60F862ULL},  {129, 0x98F1B0A679A2CA29ULL},  {240, 0x81C3C2B67F568CCFULL},
      {241, 0xC5A639ECD2030E5EULL}, {1024, 0xDD85C9B5C1109C5CULL}, {2048, 0xDD59E2C3A5F038E0ULL},
  };
  for (const Vector& v : vectors) {
    CAPTURE(v.length);
    const std::vector<std::byte> buffer = sanity_buffer(v.length);
    CHECK(wg::io::xxh3_64(std::span<const std::byte>(buffer)) == v.hash);
  }
}

TEST_CASE("io.hash string overload hashes the characters") {
  const std::string text = "worldgen";
  const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
  CHECK(wg::io::xxh3_64(text) == wg::io::xxh3_64(std::span<const std::byte>(bytes, text.size())));
  CHECK(wg::io::xxh3_64(text) != wg::io::xxh3_64(std::string_view("worldgeN")));
}
