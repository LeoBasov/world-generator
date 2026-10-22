#pragma once

// Internal little-endian packing (not installed). The world file (A09) is little-endian
// throughout; integers and doubles are written byte by byte, so the encoding does not depend on
// struct layout or host byte order. Raw field payloads are copied as they are in memory, which
// is little-endian on every supported platform (checked below).

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wg::io::detail {

static_assert(std::endian::native == std::endian::little,
              "world-file field chunks are copied verbatim and need a little-endian host");

/// Appends little-endian values to a byte vector.
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<std::byte>& out) noexcept : out_(&out) {}

  void u8(std::uint8_t v) { out_->push_back(static_cast<std::byte>(v)); }

  void u16(std::uint16_t v) { put(v, 2); }
  void u32(std::uint32_t v) { put(v, 4); }
  void u64(std::uint64_t v) { put(v, 8); }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }

  /// count zero bytes.
  void zeros(std::size_t count) { out_->insert(out_->end(), count, std::byte{0}); }

  void bytes(std::span<const std::byte> data) {
    out_->insert(out_->end(), data.begin(), data.end());
  }

 private:
  void put(std::uint64_t v, int size) {
    for (int i = 0; i < size; ++i) {
      out_->push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFU));
    }
  }

  std::vector<std::byte>* out_;
};

/// Reads little-endian values from a byte span. Reading past the end yields zeros and sets
/// failed(), so a caller checks once after a fixed-size record.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> in) noexcept : in_(in) {}

  std::uint8_t u8() noexcept { return static_cast<std::uint8_t>(get(1)); }
  std::uint16_t u16() noexcept { return static_cast<std::uint16_t>(get(2)); }
  std::uint32_t u32() noexcept { return static_cast<std::uint32_t>(get(4)); }
  std::uint64_t u64() noexcept { return get(8); }
  std::int32_t i32() noexcept { return static_cast<std::int32_t>(u32()); }
  double f64() noexcept { return std::bit_cast<double>(u64()); }

  /// The next count bytes (empty and failed() if fewer remain).
  std::span<const std::byte> bytes(std::size_t count) noexcept {
    if (count > in_.size() - pos_) {
      failed_ = true;
      pos_ = in_.size();
      return {};
    }
    std::span<const std::byte> out = in_.subspan(pos_, count);
    pos_ += count;
    return out;
  }

  [[nodiscard]] std::size_t position() const noexcept { return pos_; }
  [[nodiscard]] bool failed() const noexcept { return failed_; }

 private:
  std::uint64_t get(std::size_t size) noexcept {
    std::span<const std::byte> b = bytes(size);
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < b.size(); ++i) {
      v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    }
    return v;
  }

  std::span<const std::byte> in_;
  std::size_t pos_ = 0;
  bool failed_ = false;
};

}  // namespace wg::io::detail
