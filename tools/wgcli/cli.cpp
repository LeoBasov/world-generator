#include "cli.hpp"

#include <array>
#include <exception>
#include <string>

#include "commands.hpp"
#include "common.hpp"
#include "wg/core/version.hpp"

namespace wg::cli {

namespace {

const std::array<Command, 8> kCommands = {{
    {kGenerateSpec, run_generate},
    {kSweepSpec, run_sweep},
    {kInspectSpec, run_inspect},
    {kCompareSpec, run_compare},
    {kHashSpec, run_hash},
    {kExportSpec, run_export},
    {kValidateSpec, run_validate},
    {kBenchSpec, run_bench},
}};

const std::array<const CommandSpec*, 8> kSpecs = {&kGenerateSpec, &kSweepSpec, &kInspectSpec,
                                                  &kCompareSpec,  &kHashSpec,  &kExportSpec,
                                                  &kValidateSpec, &kBenchSpec};

const Command* find_command(std::string_view name) noexcept {
  for (const Command& c : kCommands) {
    if (c.spec.name == name) {
      return &c;
    }
  }
  return nullptr;
}

std::string general_help() {
  std::string text =
      "Usage: wgcli <command> [options]\n"
      "       wgcli help [<command>]\n"
      "       wgcli --version\n"
      "\n"
      "Command-line tool of worldgen-core: generate, sweep, inspect, compare, hash and\n"
      "export worlds.\n"
      "\n"
      "Commands:\n";
  for (const Command& c : kCommands) {
    text += "  " + std::string(c.spec.name) + std::string(10 - c.spec.name.size(), ' ') +
            std::string(c.spec.summary) + "\n";
  }
  text +=
      "\n"
      "Exit codes:\n"
      "  0  success\n"
      "  1  generation failed after all attempts (compare: the worlds differ)\n"
      "  2  invalid input: arguments, missing or invalid files\n"
      "  3  internal error\n"
      "\n"
      "Run 'wgcli <command> --help' for the options of a command.\n";
  return text;
}

int usage_error(const Io& io, std::string_view message) {
  io.err << "wgcli: " << message << "\n"
         << "Run 'wgcli help' for the list of commands.\n";
  return static_cast<int>(ExitCode::invalid_input);
}

int dispatch(std::span<const std::string_view> args, const Io& io) {
  if (args.empty()) {
    io.err << general_help();
    return static_cast<int>(ExitCode::invalid_input);
  }
  const std::string_view first = args[0];
  if (first == "--version" || first == "-V") {
    io.out << "wgcli " << core::version_string() << "\n";
    return 0;
  }
  if (first == "help" || first == "--help" || first == "-h") {
    if (args.size() > 2) {
      return usage_error(io, "help takes at most one command");
    }
    const std::string text = help_text(args.size() == 2 ? args[1] : std::string_view{});
    if (text.empty()) {
      return usage_error(io, "unknown command '" + std::string(args[1]) + "'");
    }
    io.out << text;
    return 0;
  }
  const Command* command = find_command(first);
  if (command == nullptr) {
    return usage_error(io, "unknown command '" + std::string(first) + "'");
  }
  auto parsed = parse_args(command->spec, args.subspan(1));
  if (!parsed) {
    io.err << parsed.error().message << "\n"
           << usage_line(command->spec) << "\n"
           << "Run 'wgcli " << command->spec.name << " --help' for its options.\n";
    return static_cast<int>(ExitCode::invalid_input);
  }
  if (parsed->help) {
    io.out << format_help(command->spec);
    return 0;
  }
  return command->run(*parsed, io);
}

}  // namespace

std::span<const CommandSpec* const> commands() noexcept { return kSpecs; }

std::string help_text(std::string_view command) {
  if (command.empty()) {
    return general_help();
  }
  const Command* c = find_command(command);
  return c == nullptr ? std::string() : format_help(c->spec);
}

int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) noexcept {
  try {
    const Io io{out, err};
    const int code = dispatch(args, io);
    out.flush();
    return code;
  } catch (const std::exception& e) {
    try {
      err << "wgcli: internal error: " << e.what() << "\n";
    } catch (...) {  // NOLINT(bugprone-empty-catch): nothing left to report to
    }
  } catch (...) {
    try {
      err << "wgcli: internal error\n";
    } catch (...) {  // NOLINT(bugprone-empty-catch): nothing left to report to
    }
  }
  return static_cast<int>(ExitCode::internal);
}

}  // namespace wg::cli
