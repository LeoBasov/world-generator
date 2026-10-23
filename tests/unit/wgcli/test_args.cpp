#include "args.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string_view>
#include <vector>

using wg::cli::CommandSpec;
using wg::cli::OptionKind;
using wg::cli::OptionSpec;
using wg::cli::parse_args;

namespace {

constexpr std::array kOptions = {
    OptionSpec{"seed", "N", "World seed", OptionKind::value},
    OptionSpec{"field", "NAME", "A field", OptionKind::list},
    OptionSpec{"fast", "", "Go fast", OptionKind::flag},
};

const CommandSpec kSpec{.name = "demo",
                        .summary = "Demonstrate parsing",
                        .positionals = "IN [OUT]",
                        .min_positionals = 1,
                        .max_positionals = 2,
                        .description = "A demo command.",
                        .options = kOptions};

wg::core::Expected<wg::cli::ParsedArgs> parse(std::vector<std::string_view> args) {
  return parse_args(kSpec, args);
}

}  // namespace

TEST_CASE("wgcli.args parses values, lists, flags and positionals") {
  auto p = parse({"in.wgw", "--seed", "7", "--field=elevation", "--fast", "--field", "biome",
                  "--seed=9", "out"});
  REQUIRE(p);
  CHECK_FALSE(p->help);
  CHECK(p->value("seed") == "9");
  CHECK(p->values("field") == std::vector<std::string>{"elevation", "biome"});
  CHECK(p->has("fast"));
  CHECK_FALSE(p->has("nothing"));
  CHECK_FALSE(p->value("nothing").has_value());
  CHECK(p->positionals() == std::vector<std::string>{"in.wgw", "out"});

  auto dashed = parse({"--", "--not-an-option"});
  REQUIRE(dashed);
  CHECK(dashed->positionals() == std::vector<std::string>{"--not-an-option"});

  auto help = parse({"--seed", "1", "-h"});
  REQUIRE(help);
  CHECK(help->help);
}

TEST_CASE("wgcli.args refuses malformed command lines") {
  using wg::core::StatusCode;
  const auto refused = [](std::vector<std::string_view> args, std::string_view part) {
    auto p = parse(std::move(args));
    REQUIRE_FALSE(p);
    CHECK(p.error().code == StatusCode::invalid_argument);
    CHECK(p.error().message.find(part) != std::string::npos);
  };
  refused({"in", "--bogus"}, "unknown option '--bogus'");
  refused({"in", "-x"}, "unknown option '-x'");
  refused({"in", "--seed"}, "needs N");
  refused({"in", "--fast=1"}, "takes no value");
  refused({}, "missing IN [OUT]");
  refused({"a", "b", "c"}, "unexpected argument 'c'");
}

TEST_CASE("wgcli.args formats the help of a command") {
  CHECK(wg::cli::usage_line(kSpec) == "Usage: wgcli demo IN [OUT] [options]");
  CHECK(wg::cli::format_help(kSpec) ==
        "Usage: wgcli demo IN [OUT] [options]\n"
        "\n"
        "Demonstrate parsing.\n"
        "\n"
        "A demo command.\n"
        "\n"
        "Options:\n"
        "  --seed N          World seed\n"
        "  --field NAME ...  A field\n"
        "  --fast            Go fast\n"
        "  -h, --help        Show this help and exit\n");
}

TEST_CASE("wgcli.args converts option values") {
  CHECK(wg::cli::parse_int("n", "-12") == -12);
  CHECK_FALSE(wg::cli::parse_int("n", "12x"));
  CHECK_FALSE(wg::cli::parse_int("n", ""));
  CHECK(wg::cli::parse_u64("seed", "18446744073709551615") == 18446744073709551615ULL);
  CHECK(wg::cli::parse_u64("seed", "0xFF") == 255);
  CHECK_FALSE(wg::cli::parse_u64("seed", "-1"));
  CHECK_FALSE(wg::cli::parse_u64("seed", "0x"));
  CHECK(wg::cli::parse_double("t", "0.25") == 0.25);
  CHECK_FALSE(wg::cli::parse_double("t", "nan"));
  CHECK_FALSE(wg::cli::parse_double("t", "1e999"));
  auto bad = wg::cli::parse_double("tolerance", "abc");
  REQUIRE_FALSE(bad);
  CHECK(bad.error().message == "option --tolerance: 'abc' is not a finite number");
}
