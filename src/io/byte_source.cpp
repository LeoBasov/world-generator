#include "wg/io/byte_source.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <system_error>

namespace wg::io {

namespace {

core::Status out_of_source(std::uint64_t offset, std::size_t count, std::uint64_t size) {
  return core::make_status(core::StatusCode::io_error,
                           "read of " + std::to_string(count) + " bytes at offset " +
                               std::to_string(offset) + " lies outside the " +
                               std::to_string(size) + "-byte source");
}

bool in_range(std::uint64_t offset, std::size_t count, std::uint64_t size) noexcept {
  return offset <= size && count <= size - offset;
}

}  // namespace

core::Status MemorySource::read_at(std::uint64_t offset, std::span<std::byte> out) noexcept {
  if (!in_range(offset, out.size(), data_.size())) {
    return out_of_source(offset, out.size(), data_.size());
  }
  if (!out.empty()) {
    std::memcpy(out.data(), data_.data() + offset, out.size());
  }
  return {};
}

core::Expected<FileSource> FileSource::open(const std::filesystem::path& path) noexcept {
  try {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
      return core::make_error(core::StatusCode::io_error,
                              "cannot open " + path.generic_string() + ": " + ec.message());
    }
    auto in = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*in) {
      return core::make_error(core::StatusCode::io_error, "cannot open " + path.generic_string());
    }
    return FileSource(std::move(in), static_cast<std::uint64_t>(size));
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::io_error,
                            std::string("cannot open file: ") + e.what());
  }
}

core::Status FileSource::read_at(std::uint64_t offset, std::span<std::byte> out) noexcept {
  if (!in_range(offset, out.size(), size_)) {
    return out_of_source(offset, out.size(), size_);
  }
  if (out.empty()) {
    return {};
  }
  try {
    in_->clear();
    in_->seekg(static_cast<std::streamoff>(offset));
    in_->read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!*in_ || in_->gcount() != static_cast<std::streamsize>(out.size())) {
      return core::make_status(core::StatusCode::io_error,
                               "cannot read " + std::to_string(out.size()) + " bytes at offset " +
                                   std::to_string(offset));
    }
    return {};
  } catch (const std::exception& e) {
    return core::make_status(core::StatusCode::io_error, std::string("read failed: ") + e.what());
  }
}

}  // namespace wg::io
