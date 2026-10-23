#include "args.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace wg::cli {

using core::make_error;
using core::StatusCode;

bool ParsedArgs::has(std::string_view name) const noexcept {
  return std::any_of(options_.begin(), options_.end(),
                     [&](const auto& entry) { return entry.first == name; });
}

std::optional<std::string> ParsedArgs::value(std::string_view name) const {
  std::optional<std::string> last;
  for (const auto& [key, value] : options_) {
    if (key == name) {
      last = value;
    }
  }
  return last;
}

std::vector<std::string> ParsedArgs::values(std::string_view name) const {
  std::vector<std::string> out;
  for (const auto& [key, value] : options_) {
    if (key == name) {
      out.push_back(value);
    }
  }
  return out;
}

void ParsedArgs::add(std::string_view name, std::string_view value) {
  options_.emplace_back(std::string(name), std::string(value));
}

namespace {

const OptionSpec* find_option(const CommandSpec& command, std::string_view name) noexcept {
  for (const OptionSpec& option : command.options) {
    if (option.name == name) {
      return &option;
    }
  }
  return nullptr;
}

std::string usage_error(const CommandSpec& command, std::string_view message) {
  return "wgcli " + std::string(command.name) + ": " + std::string(message);
}

}  // namespace

core::Expected<ParsedArgs> parse_args(const CommandSpec& command,
                                      std::span<const std::string_view> args) {
  ParsedArgs parsed;
  bool options_ended = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (options_ended || arg.size() < 2 || arg[0] != '-') {
      parsed.add_positional(arg);
      continue;
    }
    if (arg == "--") {
      options_ended = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      parsed.help = true;
      return parsed;
    }
    if (arg.substr(0, 2) != "--") {
      return make_error(StatusCode::invalid_argument,
                        usage_error(command, "unknown option '" + std::string(arg) + "'"));
    }
    std::string_view name = arg.substr(2);
    std::optional<std::string_view> inline_value;
    if (const std::size_t eq = name.find('='); eq != std::string_view::npos) {
      inline_value = name.substr(eq + 1);
      name = name.substr(0, eq);
    }
    const OptionSpec* option = find_option(command, name);
    if (option == nullptr) {
      return make_error(StatusCode::invalid_argument,
                        usage_error(command, "unknown option '--" + std::string(name) + "'"));
    }
    if (option->kind == OptionKind::flag) {
      if (inline_value) {
        return make_error(
            StatusCode::invalid_argument,
            usage_error(command, "option '--" + std::string(name) + "' takes no value"));
      }
      parsed.add(name, "");
      continue;
    }
    if (inline_value) {
      parsed.add(name, *inline_value);
      continue;
    }
    if (i + 1 >= args.size()) {
      return make_error(StatusCode::invalid_argument,
                        usage_error(command, "option '--" + std::string(name) + "' needs " +
                                                 std::string(option->value_name)));
    }
    parsed.add(name, args[++i]);
  }
  const std::size_t n = parsed.positionals().size();
  if (n < command.min_positionals || n > command.max_positionals) {
    std::string message = n < command.min_positionals ? "missing " : "unexpected argument ";
    message += n < command.min_positionals ? std::string(command.positionals)
                                           : "'" + parsed.positionals().back() + "'";
    return make_error(StatusCode::invalid_argument, usage_error(command, message));
  }
  return parsed;
}

std::string usage_line(const CommandSpec& command) {
  std::string line = "Usage: wgcli " + std::string(command.name);
  if (!command.positionals.empty()) {
    line += " " + std::string(command.positionals);
  }
  line += " [options]";
  return line;
}

std::string format_help(const CommandSpec& command) {
  struct Row {
    std::string left;
    std::string_view help;
  };
  std::vector<Row> rows;
  for (const OptionSpec& option : command.options) {
    std::string left = "--" + std::string(option.name);
    if (option.kind != OptionKind::flag) {
      left += " " + std::string(option.value_name);
    }
    if (option.kind == OptionKind::list) {
      left += " ...";
    }
    rows.push_back({std::move(left), option.help});
  }
  rows.push_back({"-h, --help", "Show this help and exit"});
  std::size_t width = 0;
  for (const Row& row : rows) {
    width = std::max(width, row.left.size());
  }

  std::string text = usage_line(command) + "\n\n" + std::string(command.summary) + ".\n";
  if (!command.description.empty()) {
    text += "\n" + std::string(command.description) + "\n";
  }
  text += "\nOptions:\n";
  for (const Row& row : rows) {
    text += "  " + row.left + std::string(width - row.left.size() + 2, ' ') +
            std::string(row.help) + "\n";
  }
  return text;
}

core::Expected<std::int64_t> parse_int(std::string_view option, std::string_view text) {
  std::int64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size() || text.empty()) {
    return make_error(StatusCode::invalid_argument, "option --" + std::string(option) + ": '" +
                                                        std::string(text) + "' is not an integer");
  }
  return value;
}

core::Expected<std::uint64_t> parse_u64(std::string_view option, std::string_view text) {
  std::uint64_t value = 0;
  int base = 10;
  std::string_view digits = text;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    base = 16;
    digits.remove_prefix(2);
  }
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
  if (ec != std::errc() || end != digits.data() + digits.size() || digits.empty()) {
    return make_error(StatusCode::invalid_argument, "option --" + std::string(option) + ": '" +
                                                        std::string(text) +
                                                        "' is not an unsigned 64-bit integer");
  }
  return value;
}

core::Expected<double> parse_double(std::string_view option, std::string_view text) {
  // std::strtod: floating-point std::from_chars is missing from older Apple libc++.
  const std::string copy(text);
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size() || errno == ERANGE ||
      !std::isfinite(value)) {
    return make_error(
        StatusCode::invalid_argument,
        "option --" + std::string(option) + ": '" + std::string(text) + "' is not a finite number");
  }
  return value;
}

}  // namespace wg::cli
