#include "wg/core/rng.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <set>
#include <vector>

#include "unit/core/rng_kat.inc"

using wg::core::bits64;
using wg::core::RngKey;
using wg::core::RngStream;
using wg::core::stage_key;
using wg::core::uniform01;
using wg::core::uniform_open01;

namespace kat = wg::test::kat;

namespace {

// SplitMix64 seeded with 0 (published reference output): state k * gamma, output mix64(state).
static_assert(wg::core::mix64(wg::core::kGoldenGamma) == 0xE220A8397B1DCDAFULL);
static_assert(wg::core::mix64(2 * wg::core::kGoldenGamma) == 0x6E789E6AA1B965F4ULL);
// FNV-1a 64 reference values.
static_assert(wg::core::hash_id("") == 0xCBF29CE484222325ULL);
static_assert(wg::core::hash_id("a") == 0xAF63DC4C8601EC8CULL);
// The whole key derivation is usable at compile time.
static_assert(stage_key(1, "c01_allocate", 0) != stage_key(1, "c01_allocate", 1));

std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }

}  // namespace

TEST_CASE("core.rng known-answer vectors of the hash and key derivation") {
  for (const auto& v : kat::kMix64) {
    CHECK(wg::core::mix64(v.in) == v.out);
  }
  for (const auto& v : kat::kCombine) {
    CHECK(wg::core::combine(v.h, v.w) == v.out);
  }
  for (const auto& v : kat::kHashId) {
    CHECK(wg::core::hash_id(v.text) == v.out);
  }
  for (const auto& v : kat::kStageKey) {
    CHECK(stage_key(v.seed, v.id, v.attempt).v == v.out);
  }
}

TEST_CASE("core.rng known-answer vectors of draws and uniforms") {
  for (const auto& v : kat::kDraw) {
    const RngKey key{v.key};
    CHECK(bits64(key, v.cell, v.draw) == v.bits);
    CHECK(bits(uniform01(key, v.cell, v.draw)) == v.unit01);
    CHECK(bits(uniform_open01(key, v.cell, v.draw)) == v.open01);
  }
  for (const auto& v : kat::kUniformIndex) {
    CHECK(wg::core::uniform_index(RngKey{v.key}, v.cell, v.draw, v.n) == v.out);
  }
}

TEST_CASE("core.rng unit intervals are half-open and open") {
  CHECK(wg::core::to_unit01(0) == 0.0);
  CHECK(wg::core::to_unit01(~std::uint64_t{0}) < 1.0);
  CHECK(wg::core::to_unit01(~std::uint64_t{0}) == 1.0 - 0x1p-53);
  CHECK(wg::core::to_open01(0) > 0.0);
  CHECK(wg::core::to_open01(0) == 0x1p-53);
  CHECK(wg::core::to_open01(~std::uint64_t{0}) < 1.0);

  const RngKey key = stage_key(3, "test.rng", 0);
  for (std::uint64_t i = 0; i < 10000; ++i) {
    const double u = uniform01(key, i, 0);
    const double o = uniform_open01(key, i, 0);
    REQUIRE(u >= 0.0);
    REQUIRE(u < 1.0);
    REQUIRE(o > 0.0);
    REQUIRE(o < 1.0);
    const double r = wg::core::uniform(key, i, 0, -2.0, 3.0);
    REQUIRE(r >= -2.0);
    REQUIRE(r < 3.0);
    REQUIRE(wg::core::uniform_index(key, i, 0, 7) < 7);
  }
  CHECK(wg::core::uniform_index(key, 0, 0, 0) == 0);
  CHECK(wg::core::uniform_index(key, 0, 0, 1) == 0);
}

TEST_CASE("core.rng keys differ by seed, stage, attempt, salt and name") {
  const RngKey base = stage_key(1, "c01_allocate", 0);
  std::set<std::uint64_t> keys;
  keys.insert(base.v);
  keys.insert(stage_key(2, "c01_allocate", 0).v);
  keys.insert(stage_key(1, "c02_seed", 0).v);
  keys.insert(stage_key(1, "c01_allocate", 1).v);
  keys.insert(base.derive(0).v);
  keys.insert(base.derive(1).v);
  keys.insert(base.derive("elevation").v);
  keys.insert(base.derive("rainfall").v);
  CHECK(keys.size() == 8);
  CHECK(base.derive(5) == base.derive(5));
  CHECK(base.derive("x") == base.derive(wg::core::hash_id("x")));
  // Cell and draw index are not interchangeable.
  CHECK(bits64(base, 0, 1) != bits64(base, 1, 0));
}

TEST_CASE("core.rng a stream yields the stateless draws of its id") {
  const RngKey key = stage_key(9, "test.stream", 2);
  RngStream stream{key, 66049};
  CHECK(stream.key() == key);
  CHECK(stream.id() == 66049);
  for (std::uint64_t k = 0; k < 100; ++k) {
    REQUIRE(stream.draw() == k);
    switch (k % 3) {
      case 0:
        REQUIRE(stream.next_u64() == bits64(key, 66049, k));
        break;
      case 1:
        REQUIRE(stream.uniform01() == uniform01(key, 66049, k));
        break;
      default:
        REQUIRE(stream.uniform_open01() == uniform_open01(key, 66049, k));
        break;
    }
  }
  RngStream late{key, 66049, 50};
  CHECK(late.next_u64() == bits64(key, 66049, 50));
}

TEST_CASE("core.rng draws do not depend on evaluation order") {
  // Requirement R1: a draw is a pure function of its arguments, so any schedule gives the same
  // values as the sequential one.
  const RngKey key = stage_key(11, "test.order", 0);
  constexpr std::uint64_t kCells = 4096;
  std::vector<double> forward(kCells);
  std::vector<double> backward(kCells);
  std::vector<double> strided(kCells);
  for (std::uint64_t i = 0; i < kCells; ++i) {
    forward[i] = uniform01(key, i, 3);
  }
  for (std::uint64_t i = kCells; i-- > 0;) {
    backward[i] = uniform01(key, i, 3);
  }
  for (std::uint64_t start = 0; start < 7; ++start) {
    for (std::uint64_t i = start; i < kCells; i += 7) {
      strided[i] = uniform01(key, i, 3);
    }
  }
  CHECK(forward == backward);
  CHECK(forward == strided);
}
