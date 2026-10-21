#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace wg::io {

/// XXH3-64 of the bytes with seed 0 (xxHash 0.8). Used for the parameter hash (A07), golden
/// hashes and world-file chunk checksums (A09). Identical on every platform.
std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept;

/// XXH3-64 of the characters of a string, as bytes.
std::uint64_t xxh3_64(std::string_view text) noexcept;

}  // namespace wg::io
