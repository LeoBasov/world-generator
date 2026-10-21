#include "wg/io/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The only translation unit that compiles xxHash; everything is static inline here.
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace wg::io {

std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept {
  return XXH3_64bits(data.data(), data.size());
}

std::uint64_t xxh3_64(std::string_view text) noexcept {
  return XXH3_64bits(text.data(), text.size());
}

}  // namespace wg::io
