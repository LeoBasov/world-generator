// wgcli validate: check params, profile and data files (plan section 6.7).

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "commands.hpp"
#include "wg/params/load.hpp"
#include "wg/params/validation.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"params", "FILE", "Parameter file to check", OptionKind::list},
    OptionSpec{"profile", "FILE", "Profile file to check against the stage registry",
               OptionKind::list},
    OptionSpec{"data-dir", "DIR", "Check the tables, presets and profiles of DIR",
               OptionKind::value},
};

/// Prints the issues of one file; returns true if it is valid (warnings allowed).
bool report_params(const Io& io, const std::filesystem::path& path) {
  auto text = read_file(path);
  if (!text) {
    io.out << path.string() << ": error: " << text.error().message << "\n";
    return false;
  }
  auto result = params::read_params(*text);
  if (!result) {
    io.out << path.string() << ": error: " << result.error().message << "\n";
    return false;
  }
  for (const params::ValidationIssue& issue : result->issues) {
    io.out << path.string() << ": "
           << (issue.severity == params::Severity::error ? "error" : "warning") << ": "
           << issue.path << ": " << issue.message << "\n";
  }
  const bool ok = !params::has_errors(result->issues);
  if (ok) {
    io.out << path.string() << ": ok\n";
  }
  return ok;
}

bool report_profile(const Io& io, const std::filesystem::path& path,
                    const pipeline::StageRegistry& registry) {
  auto profile = pipeline::load_profile_file(path);
  if (!profile) {
    io.out << path.string() << ": error: " << profile.error().message << "\n";
    return false;
  }
  auto pipeline = pipeline::Pipeline::create(*profile, registry);
  if (!pipeline) {
    io.out << path.string() << ": error: " << pipeline.error().message << "\n";
    return false;
  }
  io.out << path.string() << ": ok\n";
  return true;
}

/// JSON files of a directory in name order; none if it does not exist.
std::vector<std::filesystem::path> json_files(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace

const CommandSpec kValidateSpec{
    .name = "validate",
    .summary = "Check parameter, profile and data files",
    .positionals = "",
    .min_positionals = 0,
    .max_positionals = 0,
    .description =
        "Checks every rule the loaders apply: types, bounds, unknown keys and cross-field\n"
        "rules of parameters; stage ids and causal order of profiles; the data tables and the\n"
        "rules across them. Without options it checks the built-in data directory. Prints one\n"
        "line per issue with its JSON path; warnings alone do not fail.",
    .options = kOptions,
};

int run_validate(const ParsedArgs& args, const Io& io) {
  auto registry = make_registry();
  if (!registry) {
    return fail(io, registry.error());
  }
  std::vector<std::filesystem::path> params_files;
  for (const std::string& p : args.values("params")) {
    params_files.emplace_back(p);
  }
  std::vector<std::filesystem::path> profile_files;
  for (const std::string& p : args.values("profile")) {
    profile_files.emplace_back(p);
  }
  std::optional<std::filesystem::path> data_dir;
  if (auto dir = args.value("data-dir")) {
    data_dir = *dir;
  } else if (params_files.empty() && profile_files.empty()) {
    data_dir = default_data_dir();
  }

  bool ok = true;
  if (data_dir) {
    auto tables = params::load_tables_dir(*data_dir);
    if (tables) {
      io.out << data_dir->string() << ": tables ok\n";
    } else {
      io.out << data_dir->string() << ": error: " << tables.error().message << "\n";
      ok = false;
    }
    for (const auto& p : json_files(*data_dir / "presets")) {
      params_files.push_back(p);
    }
    for (const auto& p : json_files(*data_dir / "profiles")) {
      profile_files.push_back(p);
    }
  }
  for (const auto& p : params_files) {
    ok = report_params(io, p) && ok;
  }
  for (const auto& p : profile_files) {
    ok = report_profile(io, p, *registry) && ok;
  }
  return static_cast<int>(ok ? ExitCode::ok : ExitCode::invalid_input);
}

}  // namespace wg::cli
