// wgcli generate: run a profile and write a world (plan section 6.7).

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "commands.hpp"
#include "wg/io/snapshot.hpp"
#include "wg/io/world_file.hpp"
#include "world_hash.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    kProfileOption,
    kParamsOption,
    kSeedOption,
    kSizeOption,
    kThreadsOption,
    OptionSpec{"snapshots", "none|final|all",
               "Also write snapshots to OUT.snapshots/ (default none)", OptionKind::value},
    OptionSpec{"out", "FILE", "World file to write (.wgw), required", OptionKind::value},
    kDataDirOption,
    kQuietOption,
};

std::optional<io::SnapshotPolicy> parse_policy(std::string_view text) noexcept {
  if (text == "none") {
    return io::SnapshotPolicy::none;
  }
  if (text == "final") {
    return io::SnapshotPolicy::final;
  }
  if (text == "all") {
    return io::SnapshotPolicy::all;
  }
  return std::nullopt;
}

}  // namespace

const CommandSpec kGenerateSpec{
    .name = "generate",
    .summary = "Run a profile and write a world",
    .positionals = "",
    .min_positionals = 0,
    .max_positionals = 0,
    .description =
        "Runs the stages of the profile with the parameters and seed and writes the world\n"
        "file. A rejected attempt restarts with a new stage key (up to checks.max_attempts).\n"
        "Snapshots are world files named a<attempt>_<index>_<stage>.wgw: 'final' keeps the\n"
        "finished world, 'all' every completed stage of every attempt.",
    .options = kOptions,
};

int run_generate(const ParsedArgs& args, const Io& io) {
  const std::optional<std::string> out = args.value("out");
  if (!out) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "generate: option --out is required"));
  }
  const std::optional<io::SnapshotPolicy> policy =
      parse_policy(args.value("snapshots").value_or("none"));
  if (!policy) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "option --snapshots: expected none, final or all"));
  }
  auto gen = make_generator(args, io);
  if (!gen) {
    return fail(io, gen.error());
  }

  const std::filesystem::path out_path(*out);
  const std::filesystem::path snapshot_dir = out_path.string() + ".snapshots";
  io::SnapshotWriter writer(snapshot_dir, gen->params, *policy);
  StreamLogSink log(io.err, args.has("quiet") ? core::LogLevel::error : core::LogLevel::warning);
  pipeline::RunResult result =
      gen->run(gen->seed, log, *policy == io::SnapshotPolicy::none ? nullptr : &writer);
  if (!result) {
    const pipeline::FailureReport& failure = result.error();
    io.err << "wgcli: generation failed: " << core::to_string(failure.status) << "\n";
    print_rejections(io.err, failure.report.provenance);
    if (failure.status.code == core::StatusCode::failed_precondition) {
      return static_cast<int>(ExitCode::generation_failed);
    }
    return static_cast<int>(exit_code_for(failure.status));
  }

  if (core::Status s = io::write_world_file(out_path, result->state, gen->params); !s.ok()) {
    return fail(io, s);
  }
  auto hashes = hash_world(result->state);
  if (!hashes) {
    return fail(io, hashes.error());
  }
  const core::Provenance& prov = result->state.provenance();
  io.out << "world: " << out_path.string() << "\n"
         << "profile: " << prov.profile << "\n"
         << "seed: " << prov.seed << "\n"
         << "attempt: " << prov.attempt << "\n"
         << "rejections: " << prov.rejections.size() << "\n";
  print_rejections(io.out, prov);
  io.out << "params hash: " << hex64(prov.params_hash) << "\n"
         << "world hash: " << hex64(hashes->world) << "\n";
  if (*policy != io::SnapshotPolicy::none) {
    io.out << "snapshots: " << writer.written().size() << " in " << snapshot_dir.string() << "\n";
  }
  return 0;
}

}  // namespace wg::cli
