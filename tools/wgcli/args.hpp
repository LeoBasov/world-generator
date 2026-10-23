#pragma once

// Command-line parsing of wgcli (A10): a small table-driven parser, so the --help text that
// docs/cli.md reproduces is fully under our control.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"

namespace wg::cli {

/// How an option takes values.
enum class OptionKind : std::uint8_t {
  flag,   ///< no value: --per-stage
  value,  ///< one value, the last occurrence wins: --seed 7 or --seed=7
  list,   ///< repeatable, every value kept in order: --field elevation --field biome
};

/// One option of a command, e.g. {"seed", "N", "World seed (default 0)", OptionKind::value}.
struct OptionSpec {
  std::string_view name;        ///< without the leading "--"
  std::string_view value_name;  ///< placeholder in the help, empty for a flag
  std::string_view help;
  OptionKind kind = OptionKind::value;
};

/// A command: its usage, a description and its options. Every command also accepts -h/--help.
struct CommandSpec {
  std::string_view name;
  std::string_view summary;      ///< one line for the command list
  std::string_view positionals;  ///< usage of the positional arguments, e.g. "WORLD"
  std::size_t min_positionals = 0;
  std::size_t max_positionals = 0;
  std::string_view description;  ///< paragraph(s) below the usage line, already wrapped
  std::span<const OptionSpec> options;
};

/// Arguments of one command line after parsing.
class ParsedArgs {
 public:
  /// --help or -h was given; nothing else was checked.
  bool help = false;

  [[nodiscard]] bool has(std::string_view name) const noexcept;
  /// Last value of a value option, or nullopt.
  [[nodiscard]] std::optional<std::string> value(std::string_view name) const;
  /// Every value of an option, in order.
  [[nodiscard]] std::vector<std::string> values(std::string_view name) const;
  [[nodiscard]] const std::vector<std::string>& positionals() const noexcept {
    return positionals_;
  }

  void add(std::string_view name, std::string_view value);
  void add_positional(std::string_view value) { positionals_.emplace_back(value); }

 private:
  std::vector<std::pair<std::string, std::string>> options_;  ///< in command-line order
  std::vector<std::string> positionals_;
};

/// Parses the arguments that follow the command name. Accepts "--name value", "--name=value",
/// flags without a value and "--" to end the options. Errors: invalid_argument for an unknown
/// option, a missing value, a value given to a flag or a wrong number of positionals.
[[nodiscard]] core::Expected<ParsedArgs> parse_args(const CommandSpec& command,
                                                    std::span<const std::string_view> args);

/// "Usage: wgcli <name> ..." line of a command.
[[nodiscard]] std::string usage_line(const CommandSpec& command);

/// Full --help text of a command: usage, description, options. Deterministic, so docs/cli.md
/// can reproduce it byte for byte.
[[nodiscard]] std::string format_help(const CommandSpec& command);

/// Option values as numbers. Errors: invalid_argument naming the option and the text.
[[nodiscard]] core::Expected<std::int64_t> parse_int(std::string_view option,
                                                     std::string_view text);
[[nodiscard]] core::Expected<std::uint64_t> parse_u64(std::string_view option,
                                                      std::string_view text);
[[nodiscard]] core::Expected<double> parse_double(std::string_view option, std::string_view text);

}  // namespace wg::cli
