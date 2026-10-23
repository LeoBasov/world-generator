#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace wg::core {

// Counter-based random numbers (plan section 2, requirement R1). A draw is a pure function of
// (key, cell, draw index), so results do not depend on scheduling or thread count. There is no
// stateful generator to share. The layout below is frozen: changing any constant or the order
// of combination changes every world and every golden hash (docs/adr/0002-deterministic-rng.md).
// tools/rng/rng_reference.py mirrors this file bit for bit.

/// Increment of SplitMix64, 2^64 / golden ratio.
inline constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

/// SplitMix64 finaliser, a bijective 64-bit mixer.
constexpr std::uint64_t mix64(std::uint64_t z) noexcept {
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

/// Appends word w to hash state h: the concatenation H(h || w) of plan section 2.
constexpr std::uint64_t combine(std::uint64_t h, std::uint64_t w) noexcept {
  return mix64(h ^ mix64(w + kGoldenGamma));
}

/// FNV-1a 64 of an identifier's bytes, used to key the RNG by stage id or name.
constexpr std::uint64_t hash_id(std::string_view id) noexcept {
  std::uint64_t h = 0xCBF29CE484222325ULL;
  for (const char c : id) {
    h = (h ^ static_cast<std::uint8_t>(c)) * 0x100000001B3ULL;
  }
  return h;
}

/// Key of one random stream family, e.g. sigma_{s,a} of a stage attempt.
struct RngKey {
  std::uint64_t v = 0;

  /// Independent sub-key, e.g. for one field or one parameter of a stage.
  [[nodiscard]] constexpr RngKey derive(std::uint64_t salt) const noexcept {
    return {combine(v, salt)};
  }
  [[nodiscard]] constexpr RngKey derive(std::string_view name) const noexcept {
    return {combine(v, hash_id(name))};
  }

  friend constexpr bool operator==(RngKey, RngKey) = default;
};

/// Key of a world seed sigma.
constexpr RngKey seed_key(std::uint64_t seed) noexcept { return {mix64(seed)}; }

/// Key of attempt a of stage s: sigma_{s,a} = H(sigma || s || a). The stage id is the stable
/// file stem, e.g. "c02_seed".
constexpr RngKey stage_key(std::uint64_t seed, std::string_view stage_id,
                           std::uint32_t attempt) noexcept {
  return {combine(combine(seed_key(seed).v, hash_id(stage_id)), attempt)};
}

/// Raw 64 random bits H(sigma_{s,a} || i || k) for cell (or stream id) i and draw index k.
constexpr std::uint64_t bits64(RngKey key, std::uint64_t cell, std::uint64_t draw) noexcept {
  return combine(combine(key.v, cell), draw);
}

/// 2^-53 * floor(bits / 2^11): the top 53 bits as a double in [0, 1).
constexpr double to_unit01(std::uint64_t bits) noexcept {
  return static_cast<double>(bits >> 11U) * 0x1p-53;
}

/// (floor(bits / 2^12) + 1/2) * 2^-52: a double in the open interval (0, 1), for inverse-CDF
/// sampling. Uses 52 bits so that adding 1/2 is exact.
constexpr double to_open01(std::uint64_t bits) noexcept {
  return (static_cast<double>(bits >> 12U) + 0.5) * 0x1p-52;
}

/// u_{i,k} of plan section 2, in [0, 1).
constexpr double uniform01(RngKey key, std::uint64_t cell, std::uint64_t draw) noexcept {
  return to_unit01(bits64(key, cell, draw));
}

/// Uniform in (0, 1), never 0 or 1.
constexpr double uniform_open01(RngKey key, std::uint64_t cell, std::uint64_t draw) noexcept {
  return to_open01(bits64(key, cell, draw));
}

/// Uniform in [lo, hi) (hi itself may result from rounding when hi - lo is large).
constexpr double uniform(RngKey key, std::uint64_t cell, std::uint64_t draw, double lo,
                         double hi) noexcept {
  return lo + ((hi - lo) * uniform01(key, cell, draw));
}

/// Uniform integer in [0, n); 0 for n = 0. floor(u * n) with u of 53 bits, so the relative
/// bias of any value is below n / 2^53.
constexpr std::uint64_t uniform_index(RngKey key, std::uint64_t cell, std::uint64_t draw,
                                      std::uint64_t n) noexcept {
  if (n == 0) {
    return 0;
  }
  const auto r = static_cast<std::uint64_t>(uniform01(key, cell, draw) * static_cast<double>(n));
  return std::min(r, n - 1);
}

/// Sequential draws k = first, first + 1, ... of one stream id, for processes that need a
/// variable number of draws (river walkers, Markov chains, rejection sampling). A stream is a
/// small local value and is never shared between threads; its k-th draw equals
/// bits64(key, id, k), so it adds no state beyond the draw counter.
class RngStream {
 public:
  constexpr RngStream(RngKey key, std::uint64_t id, std::uint64_t first_draw = 0) noexcept
      : key_(key), id_(id), draw_(first_draw) {}

  constexpr std::uint64_t next_u64() noexcept { return bits64(key_, id_, draw_++); }
  constexpr double uniform01() noexcept { return to_unit01(next_u64()); }
  constexpr double uniform_open01() noexcept { return to_open01(next_u64()); }

  [[nodiscard]] constexpr RngKey key() const noexcept { return key_; }
  [[nodiscard]] constexpr std::uint64_t id() const noexcept { return id_; }
  /// Index of the next draw (= number of draws taken when the stream started at 0).
  [[nodiscard]] constexpr std::uint64_t draw() const noexcept { return draw_; }

 private:
  RngKey key_;
  std::uint64_t id_;
  std::uint64_t draw_;
};

}  // namespace wg::core
