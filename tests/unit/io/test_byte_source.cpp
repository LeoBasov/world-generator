#include "wg/io/byte_source.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "support/temp_dir.hpp"

using wg::core::StatusCode;
using wg::io::FileSource;
using wg::io::MemorySource;

namespace {

std::vector<std::byte> sequence(std::size_t n) {
  std::vector<std::byte> b(n);
  for (std::size_t i = 0; i < n; ++i) {
    b[i] = static_cast<std::byte>(i);
  }
  return b;
}

}  // namespace

TEST_CASE("io.byte_source memory source reads ranges inside the buffer") {
  const std::vector<std::byte> data = sequence(10);
  MemorySource src(data);
  CHECK(src.size() == 10);
  std::vector<std::byte> out(3);
  REQUIRE(src.read_at(4, out).ok());
  CHECK(out == std::vector<std::byte>{std::byte{4}, std::byte{5}, std::byte{6}});
  CHECK(src.read_at(10, std::span<std::byte>{}).ok());
  const wg::core::Status s = src.read_at(8, out);
  CHECK(s.code == StatusCode::io_error);
  CHECK(s.message == "read of 3 bytes at offset 8 lies outside the 10-byte source");
  CHECK(src.read_at(std::numeric_limits<std::uint64_t>::max(), out).code == StatusCode::io_error);
}

TEST_CASE("io.byte_source file source reads ranges of a file") {
  const wg::test::TempDir dir;
  const std::vector<std::byte> data = sequence(200);
  {
    std::ofstream f(dir / "data.bin", std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  auto src = FileSource::open(dir / "data.bin");
  REQUIRE(src);
  CHECK(src->size() == 200);
  std::vector<std::byte> out(2);
  REQUIRE(src->read_at(198, out).ok());
  CHECK(out == std::vector<std::byte>{std::byte{198}, std::byte{199}});
  REQUIRE(src->read_at(0, out).ok());  // seeking back
  CHECK(out == std::vector<std::byte>{std::byte{0}, std::byte{1}});
  CHECK(src->read_at(199, out).code == StatusCode::io_error);
  REQUIRE(src->read_at(5, out).ok());  // a failed read does not poison the stream
  CHECK(out[0] == std::byte{5});

  FileSource moved = std::move(*src);
  CHECK(moved.size() == 200);

  auto missing = FileSource::open(dir / "missing.bin");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == StatusCode::io_error);
  CHECK(missing.error().message.find("missing.bin") != std::string::npos);
}
