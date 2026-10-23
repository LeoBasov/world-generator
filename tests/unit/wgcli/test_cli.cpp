#include "cli.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/cli_runner.hpp"
#include "wg/core/version.hpp"

using wg::test::contains;
using wg::test::run_cli;

TEST_CASE("wgcli.cli prints the version and the general help") {
  const auto v = run_cli({"--version"});
  CHECK(v.code == 0);
  CHECK(v.out == "wgcli " + std::string(wg::core::version_string()) + "\n");

  const auto help = run_cli({"help"});
  CHECK(help.code == 0);
  CHECK(help.out == wg::cli::help_text(""));
  for (const wg::cli::CommandSpec* c : wg::cli::commands()) {
    CHECK(contains(help.out, "  " + std::string(c->name) + " "));
  }
  CHECK(run_cli({"--help"}).out == help.out);
}

TEST_CASE("wgcli.cli prints the help of every command") {
  REQUIRE(wg::cli::commands().size() == 8);
  for (const wg::cli::CommandSpec* c : wg::cli::commands()) {
    const std::string name(c->name);
    const auto r = run_cli({name, "--help"});
    CHECK(r.code == 0);
    CHECK(r.out == wg::cli::help_text(name));
    CHECK(r.out.rfind("Usage: wgcli " + name, 0) == 0);
    CHECK(run_cli({"help", name}).out == r.out);
  }
  CHECK(wg::cli::help_text("bogus").empty());
}

TEST_CASE("wgcli.cli exits with 2 for unusable command lines") {
  const auto none = run_cli({});
  CHECK(none.code == 2);
  CHECK(contains(none.err, "Usage: wgcli <command>"));
  const auto unknown = run_cli({"bogus"});
  CHECK(unknown.code == 2);
  CHECK(contains(unknown.err, "unknown command 'bogus'"));
  CHECK(run_cli({"help", "bogus"}).code == 2);
  CHECK(run_cli({"help", "generate", "sweep"}).code == 2);
  const auto option = run_cli({"inspect", "w.wgw", "--bogus"});
  CHECK(option.code == 2);
  CHECK(contains(option.err, "Usage: wgcli inspect WORLD [options]"));
}
