#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cli.hpp"
#include "mock_profile.hpp"

namespace wg::test {

// Runs wgcli commands in-process for the A10 integration tests.

/// Exit code and both output streams of one command line.
struct CliResult {
  int code = -1;
  std::string out;
  std::string err;
};

inline CliResult run_cli(const std::vector<std::string>& args) {
  std::vector<std::string_view> views(args.begin(), args.end());
  std::ostringstream out;
  std::ostringstream err;
  CliResult r;
  r.code = cli::run(views, out, err);
  r.out = out.str();
  r.err = err.str();
  return r;
}

/// The mock profile of tests/fixtures/profiles/mock.json.
inline std::string mock_profile() {
  // Forward slashes on every platform, so the path can be embedded in JSON.
  return (std::filesystem::path(WG_TEST_SOURCE_DIR) / "tests/fixtures/profiles/mock.json")
      .generic_string();
}

/// Grid size of the CLI tests: small, so every command runs in milliseconds.
inline constexpr std::string_view kCliSize = "33";

/// First seed from start on whose attempt 0 m03_verify rejects (want_reject) or accepts, and
/// whose attempt 1 accepts.
inline std::uint64_t mock_seed(bool want_reject, std::uint64_t start = 0) {
  for (std::uint64_t s = start;; ++s) {
    if (cli::mock_verify_rejects(s, 0) == want_reject && !cli::mock_verify_rejects(s, 1)) {
      return s;
    }
  }
}

/// True if text contains part.
inline bool contains(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

}  // namespace wg::test
