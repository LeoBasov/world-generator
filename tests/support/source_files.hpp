#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace wg::test {

// Files of the source tree (data/, schema/) for tests of the shipped data. WG_TEST_SOURCE_DIR
// is the repository root, set by tests/CMakeLists.txt.

/// Path of a file relative to the repository root.
inline std::filesystem::path source_path(std::string_view relative) {
  return std::filesystem::path(WG_TEST_SOURCE_DIR) / std::filesystem::path(relative);
}

/// Contents of a file relative to the repository root, empty if it cannot be read.
inline std::string read_source_file(std::string_view relative) {
  std::ifstream in(source_path(relative), std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

/// The .json files of a directory relative to the repository root, sorted by name.
inline std::vector<std::filesystem::path> json_files(std::string_view relative_dir) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(source_path(relative_dir))) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace wg::test
