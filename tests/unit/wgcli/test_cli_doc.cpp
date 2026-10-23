#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "cli.hpp"

// docs/cli.md reproduces every --help text between markers (plan A10: ci.yml fails if the
// reference and the --help output disagree). Resync with tools/wgcli/sync_cli_doc.py.

namespace {

constexpr std::string_view kEnd = "<!-- END -->";

/// Help blocks of the reference by key ("help" for the general help, else the command name).
std::map<std::string, std::string> help_blocks(const std::string& doc) {
  std::map<std::string, std::string> blocks;
  const std::string begin = "<!-- BEGIN wgcli ";
  for (std::size_t pos = doc.find(begin); pos != std::string::npos;
       pos = doc.find(begin, pos + 1)) {
    const std::size_t name_start = pos + begin.size();
    const std::size_t name_end = doc.find(' ', name_start);
    const std::size_t body_start = doc.find("```text\n", name_end);
    const std::size_t body_end = doc.find("```\n" + std::string(kEnd), body_start);
    REQUIRE(name_end != std::string::npos);
    REQUIRE(body_start != std::string::npos);
    REQUIRE(body_end != std::string::npos);
    const std::size_t text_start = body_start + 8;
    blocks[doc.substr(name_start, name_end - name_start)] =
        doc.substr(text_start, body_end - text_start);
  }
  return blocks;
}

}  // namespace

TEST_CASE("wgcli.docs cli.md matches the --help output") {
  const std::filesystem::path path = std::filesystem::path(WG_TEST_SOURCE_DIR) / "docs/cli.md";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  std::stringstream text;
  text << in.rdbuf();
  const std::map<std::string, std::string> blocks = help_blocks(text.str());

  const std::string fix =
      "docs/cli.md is out of date: run python tools/wgcli/sync_cli_doc.py --wgcli <path to "
      "wgcli>";
  INFO(fix);
  REQUIRE(blocks.count("help") == 1);
  CHECK(blocks.at("help") == wg::cli::help_text(""));
  for (const wg::cli::CommandSpec* c : wg::cli::commands()) {
    const std::string name(c->name);
    CAPTURE(name);
    REQUIRE(blocks.count(name) == 1);
    CHECK(blocks.at(name) == wg::cli::help_text(name));
  }
  CHECK(blocks.size() == wg::cli::commands().size() + 1);  // no stale blocks
}
