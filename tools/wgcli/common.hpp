#pragma once

// Plumbing shared by the wgcli commands (A10): exit codes, loading of parameters, profiles and
// tables, the executor, the stage registry and the log sink.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "args.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/sinks.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/params/params.hpp"
#include "wg/params/tables.hpp"
#include "wg/pipeline/pipeline.hpp"

namespace wg::cli {

/// Exit codes of plan section 6.7.
enum class ExitCode : int {
  ok = 0,
  generation_failed = 1,  ///< every attempt was rejected (compare: the worlds differ)
  invalid_input = 2,      ///< bad arguments, unreadable or invalid files
  internal = 3,           ///< a bug or an exhausted resource
};

/// Output streams of a command.
struct Io {
  std::ostream& out;
  std::ostream& err;
};

/// Exit code of a failed call on user input: internal and out_of_memory are 3, everything else
/// (invalid arguments, missing or malformed files, newer versions) is 2.
[[nodiscard]] ExitCode exit_code_for(const core::Status& status) noexcept;

/// Prints "wgcli: <message>" to err and returns exit_code_for(status).
int fail(const Io& io, const core::Status& status);

/// Data directory compiled into the tool (the source tree's data/).
[[nodiscard]] std::filesystem::path default_data_dir();

/// --data-dir, or default_data_dir().
[[nodiscard]] std::filesystem::path data_dir_option(const ParsedArgs& args);

/// Reads a whole file. Errors: io_error naming the path.
[[nodiscard]] core::Expected<std::string> read_file(const std::filesystem::path& path);

/// Writes text to a file, creating its parent directory. Errors: io_error naming the path.
[[nodiscard]] core::Status write_file(const std::filesystem::path& path, std::string_view text);

/// A file path, or a name looked up as <dir>/<name>.json. Errors: not_found naming both.
[[nodiscard]] core::Expected<std::filesystem::path> resolve_named_file(
    std::string_view what, std::string_view name_or_path, const std::filesystem::path& dir);

/// Seeds of "7", "1..1000" or "1,5,10..12". Errors: invalid_argument; more than 10^7 seeds.
[[nodiscard]] core::Expected<std::vector<std::uint64_t>> parse_seed_list(std::string_view text);

/// Executor for --threads: SerialExecutor for 1, a ThreadPool otherwise (0 = one per hardware
/// thread). Errors: invalid_argument outside [0, ThreadPool::kMaxThreads].
[[nodiscard]] core::Expected<std::unique_ptr<core::Executor>> make_executor(std::int64_t threads);

/// Registry of every stage a profile may name: the library's stages (from A11) and the mock
/// stages of mock_profile.hpp.
[[nodiscard]] core::Expected<pipeline::StageRegistry> make_registry();

/// Log sink writing "wgcli: <level>: <message>" lines to a stream from any thread.
class StreamLogSink final : public core::LogSink {
 public:
  StreamLogSink(std::ostream& stream, core::LogLevel min_level) noexcept
      : stream_(&stream), min_level_(min_level) {}
  void write(core::LogLevel level, std::string_view message) noexcept override;
  [[nodiscard]] bool enabled(core::LogLevel level) const noexcept override {
    return level >= min_level_;
  }

 private:
  std::mutex mutex_;
  std::ostream* stream_;
  core::LogLevel min_level_;
};

/// Options of the commands that generate worlds (generate, sweep, hash, bench).
inline constexpr OptionSpec kProfileOption{
    "profile", "NAME|FILE", "Profile name in DIR/profiles, or a profile file", OptionKind::value};
inline constexpr OptionSpec kParamsOption{
    "params", "NAME|FILE", "Preset name in DIR/presets, or a params file", OptionKind::value};
inline constexpr OptionSpec kSeedOption{
    "seed", "N", "World seed (default: the params file's, else 0)", OptionKind::value};
inline constexpr OptionSpec kSizeOption{"size", "N", "Grid size N = 2^k + 1, overrides grid.size",
                                        OptionKind::value};
inline constexpr OptionSpec kThreadsOption{
    "threads", "T", "Worker threads, 0 = one per core (default 1)", OptionKind::value};
inline constexpr OptionSpec kDataDirOption{
    "data-dir", "DIR", "Data directory (default: the built-in one)", OptionKind::value};
inline constexpr OptionSpec kQuietOption{"quiet", "", "Print warnings only on failure",
                                         OptionKind::flag};

/// Everything a generating command needs: the checked pipeline, the parameters, the tables and
/// the executor, built from the options above.
struct Generator {
  pipeline::Pipeline pipeline;
  params::GenParams params;
  params::DataTables tables;
  std::unique_ptr<core::Executor> exec;
  /// --seed, else the params file's seed, else 0.
  std::uint64_t seed = 0;

  /// Runs the pipeline for seed.
  [[nodiscard]] pipeline::RunResult run(std::uint64_t run_seed, core::LogSink& log,
                                        pipeline::SnapshotHook* snapshot = nullptr);
};

/// Inputs of a Generator as option text; unset values take their defaults.
struct GeneratorOptions {
  std::optional<std::string> profile;
  std::optional<std::string> params;
  std::optional<std::string> seed;
  std::optional<std::string> size;
  std::optional<std::string> threads;
  std::filesystem::path data_dir;
  bool quiet = false;
};

/// --profile, --params, --seed, --size, --threads, --data-dir and --quiet.
[[nodiscard]] GeneratorOptions generator_options(const ParsedArgs& args);

/// Builds a Generator. Parameter warnings go to io.err unless quiet. Errors: those of the
/// loaders (with the file named), invalid_argument for invalid parameters (one issue per line)
/// or a missing profile, those of Pipeline::create.
[[nodiscard]] core::Expected<Generator> make_generator(const GeneratorOptions& options,
                                                       const Io& io);
[[nodiscard]] core::Expected<Generator> make_generator(const ParsedArgs& args, const Io& io);

/// Loads parameters from a preset name or file (defaults if empty) and applies a grid size.
/// Errors as make_generator.
[[nodiscard]] core::Expected<params::ParamsDocument> load_run_params(
    const std::optional<std::string>& params_arg, const std::optional<std::string>& size_arg,
    const std::filesystem::path& data_dir);

/// Loads a profile from a name or file.
[[nodiscard]] core::Expected<pipeline::Profile> load_run_profile(
    std::string_view name_or_path, const std::filesystem::path& data_dir);

/// Prints the rejections of a provenance, one per line, indented by two spaces.
void print_rejections(std::ostream& out, const core::Provenance& provenance);

}  // namespace wg::cli
