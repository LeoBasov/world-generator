#include "common.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "mock_profile.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/params/load.hpp"
#include "wg/params/validation.hpp"

#ifndef WGCLI_DATA_DIR
#define WGCLI_DATA_DIR "data"
#endif

namespace wg::cli {

using core::make_error;
using core::StatusCode;

ExitCode exit_code_for(const core::Status& status) noexcept {
  switch (status.code) {
    case StatusCode::ok:
      return ExitCode::ok;
    case StatusCode::internal:
    case StatusCode::out_of_memory:
    case StatusCode::cancelled:
      return ExitCode::internal;
    default:
      return ExitCode::invalid_input;
  }
}

int fail(const Io& io, const core::Status& status) {
  io.err << "wgcli: " << status.message << "\n";
  return static_cast<int>(exit_code_for(status));
}

std::filesystem::path default_data_dir() { return {WGCLI_DATA_DIR}; }

std::filesystem::path data_dir_option(const ParsedArgs& args) {
  if (auto dir = args.value("data-dir")) {
    return {*dir};
  }
  return default_data_dir();
}

core::Expected<std::string> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_error(StatusCode::io_error, "cannot open " + path.string());
  }
  std::ostringstream text;
  text << in.rdbuf();
  if (in.bad()) {
    return make_error(StatusCode::io_error, "cannot read " + path.string());
  }
  return text.str();
}

core::Status write_file(const std::filesystem::path& path, std::string_view text) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (!out) {
    return core::make_status(StatusCode::io_error, "cannot write " + path.string());
  }
  return {};
}

core::Expected<std::filesystem::path> resolve_named_file(std::string_view what,
                                                         std::string_view name_or_path,
                                                         const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::path as_path(name_or_path);
  if (std::filesystem::is_regular_file(as_path, ec)) {
    return as_path;
  }
  std::filesystem::path named = dir / (std::string(name_or_path) + ".json");
  if (std::filesystem::is_regular_file(named, ec)) {
    return named;
  }
  return make_error(StatusCode::not_found, std::string(what) + " '" + std::string(name_or_path) +
                                               "' not found: no such file and no " +
                                               named.string());
}

core::Expected<std::vector<std::uint64_t>> parse_seed_list(std::string_view text) {
  constexpr std::uint64_t kMaxSeeds = 10'000'000;
  std::vector<std::uint64_t> seeds;
  if (text.empty()) {
    return make_error(StatusCode::invalid_argument, "option --seeds: empty seed list");
  }
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t comma = text.find(',', start);
    if (comma == std::string_view::npos) {
      comma = text.size();
    }
    const std::string_view item = text.substr(start, comma - start);
    const std::size_t dots = item.find("..");
    auto lo = parse_u64("seeds", item.substr(0, dots));
    if (!lo) {
      return make_error(std::move(lo.error()));
    }
    std::uint64_t hi = *lo;
    if (dots != std::string_view::npos) {
      auto parsed_hi = parse_u64("seeds", item.substr(dots + 2));
      if (!parsed_hi) {
        return make_error(std::move(parsed_hi.error()));
      }
      hi = *parsed_hi;
      if (hi < *lo) {
        return make_error(StatusCode::invalid_argument,
                          "option --seeds: empty range '" + std::string(item) + "'");
      }
    }
    if (hi - *lo >= kMaxSeeds || seeds.size() + (hi - *lo) >= kMaxSeeds) {
      return make_error(StatusCode::invalid_argument, "option --seeds: more than 10^7 seeds");
    }
    for (std::uint64_t s = *lo;; ++s) {
      seeds.push_back(s);
      if (s == hi) {
        break;
      }
    }
    start = comma + 1;
  }
  return seeds;
}

core::Expected<std::unique_ptr<core::Executor>> make_executor(std::int64_t threads) {
  if (threads < 0 || threads > core::ThreadPool::kMaxThreads) {
    return make_error(StatusCode::invalid_argument,
                      "option --threads: " + std::to_string(threads) + " is outside [0, " +
                          std::to_string(core::ThreadPool::kMaxThreads) + "]");
  }
  if (threads == 1) {
    return std::unique_ptr<core::Executor>(std::make_unique<core::SerialExecutor>());
  }
  auto pool = core::ThreadPool::create(static_cast<std::int32_t>(threads));
  if (!pool) {
    return make_error(std::move(pool.error()));
  }
  return std::unique_ptr<core::Executor>(std::move(*pool));
}

core::Expected<pipeline::StageRegistry> make_registry() {
  pipeline::StageRegistry registry;
  // The library's stage adders join here: the classic stages from A11, the physical ones later.
  if (core::Status s = add_mock_stages(registry); !s.ok()) {
    return make_error(std::move(s));
  }
  return registry;
}

void StreamLogSink::write(core::LogLevel level, std::string_view message) noexcept {
  if (!enabled(level)) {
    return;
  }
  try {
    const std::lock_guard<std::mutex> lock(mutex_);
    *stream_ << "wgcli: " << core::to_string(level) << ": " << message << "\n";
  } catch (...) {  // NOLINT(bugprone-empty-catch): a lost log line must not stop a run
  }
}

core::Expected<params::ParamsDocument> load_run_params(const std::optional<std::string>& params_arg,
                                                       const std::optional<std::string>& size_arg,
                                                       const std::filesystem::path& data_dir) {
  params::ParamsDocument document;
  if (params_arg) {
    auto path = resolve_named_file("params", *params_arg, data_dir / "presets");
    if (!path) {
      return make_error(std::move(path.error()));
    }
    auto loaded = params::load_params_file(*path);
    if (!loaded) {
      return make_error(loaded.error().code, path->string() + ": " + loaded.error().message);
    }
    document = std::move(*loaded);
  }
  if (size_arg) {
    auto size = parse_int("size", *size_arg);
    if (!size) {
      return make_error(std::move(size.error()));
    }
    if (*size < 0 || *size > 1'000'000) {
      return make_error(StatusCode::invalid_argument,
                        "option --size: " + *size_arg + " is out of range");
    }
    document.params.grid.size = static_cast<std::int32_t>(*size);
  }
  const std::vector<params::ValidationIssue> issues = params::validate(document.params);
  if (params::has_errors(issues)) {
    return make_error(StatusCode::invalid_argument,
                      "invalid parameters:\n" + params::format_issues(issues));
  }
  return document;
}

core::Expected<pipeline::Profile> load_run_profile(std::string_view name_or_path,
                                                   const std::filesystem::path& data_dir) {
  auto path = resolve_named_file("profile", name_or_path, data_dir / "profiles");
  if (!path) {
    return make_error(std::move(path.error()));
  }
  auto profile = pipeline::load_profile_file(*path);
  if (!profile) {
    return make_error(profile.error().code, path->string() + ": " + profile.error().message);
  }
  return std::move(*profile);
}

pipeline::RunResult Generator::run(std::uint64_t run_seed, core::LogSink& log,
                                   pipeline::SnapshotHook* snapshot) {
  const pipeline::RunContext ctx{.exec = *exec, .log = log, .snapshot = snapshot};
  return pipeline.run(params, run_seed, ctx, pipeline::RunInputs{.tables = &tables});
}

GeneratorOptions generator_options(const ParsedArgs& args) {
  return GeneratorOptions{.profile = args.value("profile"),
                          .params = args.value("params"),
                          .seed = args.value("seed"),
                          .size = args.value("size"),
                          .threads = args.value("threads"),
                          .data_dir = data_dir_option(args),
                          .quiet = args.has("quiet")};
}

core::Expected<Generator> make_generator(const ParsedArgs& args, const Io& io) {
  return make_generator(generator_options(args), io);
}

core::Expected<Generator> make_generator(const GeneratorOptions& options, const Io& io) {
  const std::filesystem::path& data_dir = options.data_dir;
  auto document = load_run_params(options.params, options.size, data_dir);
  if (!document) {
    return make_error(std::move(document.error()));
  }
  if (!options.quiet) {
    const auto issues = params::validate(document->params);
    if (!issues.empty()) {
      io.err << params::format_issues(issues);
    }
  }

  std::optional<std::string> profile_name = options.profile;
  if (!profile_name) {
    profile_name = document->profile;
  }
  if (!profile_name) {
    return make_error(StatusCode::invalid_argument,
                      "no profile: pass --profile or name one in the params file");
  }
  auto profile = load_run_profile(*profile_name, data_dir);
  if (!profile) {
    return make_error(std::move(profile.error()));
  }

  auto tables = params::load_tables_dir(data_dir);
  if (!tables) {
    return make_error(tables.error().code,
                      "data tables in " + data_dir.string() + ": " + tables.error().message);
  }

  std::int64_t threads = 1;
  if (const auto& t = options.threads) {
    auto parsed = parse_int("threads", *t);
    if (!parsed) {
      return make_error(std::move(parsed.error()));
    }
    threads = *parsed;
  }
  auto exec = make_executor(threads);
  if (!exec) {
    return make_error(std::move(exec.error()));
  }

  std::uint64_t seed = document->seed.value_or(0);
  if (const auto& s = options.seed) {
    auto parsed = parse_u64("seed", *s);
    if (!parsed) {
      return make_error(std::move(parsed.error()));
    }
    seed = *parsed;
  }

  auto registry = make_registry();
  if (!registry) {
    return make_error(std::move(registry.error()));
  }
  auto pipeline = pipeline::Pipeline::create(*profile, *registry);
  if (!pipeline) {
    return make_error(std::move(pipeline.error()));
  }
  return Generator{.pipeline = std::move(*pipeline),
                   .params = document->params,
                   .tables = std::move(*tables),
                   .exec = std::move(*exec),
                   .seed = seed};
}

void print_rejections(std::ostream& out, const core::Provenance& provenance) {
  for (const core::RejectionRecord& r : provenance.rejections) {
    out << "  attempt " << r.attempt << ": " << r.stage << " " << r.check_id
        << " measured=" << r.measured << " required=" << r.required << "\n";
  }
}

}  // namespace wg::cli
