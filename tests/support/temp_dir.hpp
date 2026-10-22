#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace wg::test {

/// A fresh directory under the system temp directory, removed with its contents on
/// destruction. The name is random, so test cases running in parallel processes never share one.
class TempDir {
 public:
  explicit TempDir(std::string_view tag = "wg_test") {
    std::random_device device;
    const auto ticks =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t salt = (static_cast<std::uint64_t>(device()) << 32) ^ ticks;
    path_ =
        std::filesystem::temp_directory_path() / (std::string(tag) + "_" + std::to_string(salt));
    std::filesystem::create_directories(path_);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::filesystem::path operator/(std::string_view name) const {
    return path_ / std::filesystem::path(name);
  }

 private:
  std::filesystem::path path_;
};

}  // namespace wg::test
