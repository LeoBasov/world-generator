#pragma once

// Canonical byte encoding of the parameter and table hashes (ADR 0005): little-endian
// integers, length-prefixed strings and doubles by their bits with -0.0 as +0.0 and one NaN, so
// the bytes and the hash are the same on every platform.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "wg/io/hash.hpp"

namespace wg::params::detail {

class Encoder {
 public:
  void u8(std::uint8_t v) { bytes_.push_back(static_cast<std::byte>(v)); }

  void u64(std::uint64_t v) {
    for (int shift = 0; shift < 64; shift += 8) {
      bytes_.push_back(static_cast<std::byte>((v >> shift) & 0xFFU));
    }
  }

  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }

  void real(double v) {
    if (v == 0.0) {
      u64(0);  // -0.0 and +0.0
    } else if (std::isnan(v)) {
      u64(0x7FF8000000000000ULL);
    } else {
      u64(std::bit_cast<std::uint64_t>(v));
    }
  }

  void text(std::string_view s) {
    u64(s.size());
    for (char c : s) {
      bytes_.push_back(static_cast<std::byte>(c));
    }
  }

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::uint64_t hash() const noexcept { return io::xxh3_64(bytes()); }

 private:
  std::vector<std::byte> bytes_;
};

}  // namespace wg::params::detail
