#pragma once

// Entry point of wgcli (A10, plan section 6.7). main() only forwards argv here, so the tests run
// every command in-process.

#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "args.hpp"

namespace wg::cli {

/// Runs one command line (without the program name) and returns the exit code of plan 6.7:
/// 0 success, 1 generation failed after all attempts, 2 invalid input, 3 internal error.
/// Results go to out, diagnostics to err. Never throws.
[[nodiscard]] int run(std::span<const std::string_view> args, std::ostream& out,
                      std::ostream& err) noexcept;

/// Every command, in the order of the general help.
[[nodiscard]] std::span<const CommandSpec* const> commands() noexcept;

/// The output of "wgcli <command> --help", or of "wgcli help" for an empty command. Empty for an
/// unknown command.
[[nodiscard]] std::string help_text(std::string_view command);

}  // namespace wg::cli
