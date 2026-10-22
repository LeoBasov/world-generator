#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <utility>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"

namespace wg::io {

/// Random-access bytes a world file is read from (A09). The reader asks only for the ranges it
/// needs, so a partial read touches only the header, the chunk table, the metadata and the
/// requested chunks. Implementations: MemorySource for a buffer, FileSource for a file; the
/// Godot binding (Phase 2) wraps FileAccess, because exported builds read packed data (plan
/// 6.6). Not thread-safe: one reader at a time.
class ByteSource {
 public:
  virtual ~ByteSource() = default;

  /// Total size in bytes.
  [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;

  /// Fills out with the bytes at [offset, offset + out.size()). Errors: io_error if the range
  /// lies outside the source or reading fails.
  virtual core::Status read_at(std::uint64_t offset, std::span<std::byte> out) noexcept = 0;

 protected:
  ByteSource() = default;
  ByteSource(const ByteSource&) = default;
  ByteSource& operator=(const ByteSource&) = default;
  ByteSource(ByteSource&&) = default;
  ByteSource& operator=(ByteSource&&) = default;
};

/// A caller-owned buffer; it must outlive the source.
class MemorySource final : public ByteSource {
 public:
  explicit MemorySource(std::span<const std::byte> data) noexcept : data_(data) {}

  [[nodiscard]] std::uint64_t size() const noexcept override { return data_.size(); }
  core::Status read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override;

 private:
  std::span<const std::byte> data_;
};

/// A file opened for reading.
class FileSource final : public ByteSource {
 public:
  /// Opens path. Errors: io_error naming the path.
  [[nodiscard]] static core::Expected<FileSource> open(const std::filesystem::path& path) noexcept;

  [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
  core::Status read_at(std::uint64_t offset, std::span<std::byte> out) noexcept override;

 private:
  FileSource(std::unique_ptr<std::ifstream> in, std::uint64_t size) noexcept
      : in_(std::move(in)), size_(size) {}

  std::unique_ptr<std::ifstream> in_;
  std::uint64_t size_;
};

}  // namespace wg::io
