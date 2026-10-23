// wgcli bench: timed runs per stage (plan sections 6.7 and 9.1).

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"repeat", "R", "Runs per stage (default 3)", OptionKind::value},
    OptionSpec{"json", "FILE", "Also write the table as JSON", OptionKind::value},
    kProfileOption,
    kParamsOption,
    kSeedOption,
    kSizeOption,
    kThreadsOption,
    kDataDirOption,
    kQuietOption,
};

struct StageTimes {
  std::vector<std::int64_t> wall_ns;
  std::size_t bytes = 0;  ///< largest bytes_owned_after
};

}  // namespace

const CommandSpec kBenchSpec{
    .name = "bench",
    .summary = "Time every stage over repeated runs",
    .positionals = "",
    .min_positionals = 0,
    .max_positionals = 0,
    .description =
        "Generates the same world R times and prints min, median and max wall time and the\n"
        "memory owned after each stage of the successful attempt. Times vary between runs\n"
        "and machines; they never enter a hash.",
    .options = kOptions,
};

int run_bench(const ParsedArgs& args, const Io& io) {
  std::int64_t repeat = 3;
  if (auto text = args.value("repeat")) {
    auto parsed = parse_int("repeat", *text);
    if (!parsed) {
      return fail(io, parsed.error());
    }
    if (*parsed < 1 || *parsed > 100000) {
      return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                        "option --repeat: R must be in [1, 100000]"));
    }
    repeat = *parsed;
  }
  auto gen = make_generator(args, io);
  if (!gen) {
    return fail(io, gen.error());
  }

  StreamLogSink log(io.err, core::LogLevel::error);
  std::vector<std::string> order;
  std::map<std::string, StageTimes> times;
  for (std::int64_t r = 0; r < repeat; ++r) {
    pipeline::RunResult result = gen->run(gen->seed, log);
    if (!result) {
      io.err << "wgcli: generation failed: " << core::to_string(result.error().status) << "\n";
      return static_cast<int>(result.error().status.code == core::StatusCode::failed_precondition
                                  ? ExitCode::generation_failed
                                  : exit_code_for(result.error().status));
    }
    const std::uint32_t attempt = result->report.provenance.attempt;
    for (const pipeline::StageRecord& rec : result->report.stages) {
      if (rec.attempt != attempt) {
        continue;
      }
      if (times.find(rec.stage) == times.end()) {
        order.push_back(rec.stage);
      }
      StageTimes& t = times[rec.stage];
      t.wall_ns.push_back(rec.metrics.wall_ns);
      t.bytes = std::max(t.bytes, rec.metrics.bytes_owned_after);
    }
  }

  nlohmann::ordered_json stages = nlohmann::ordered_json::array();
  io.out << std::left << std::setw(24) << "stage" << std::right << std::setw(12) << "min_ms"
         << std::setw(12) << "median_ms" << std::setw(12) << "max_ms" << std::setw(12) << "mem_mib"
         << "\n"
         << std::fixed << std::setprecision(3);
  for (const std::string& stage : order) {
    std::vector<std::int64_t> w = times[stage].wall_ns;
    std::sort(w.begin(), w.end());
    const double min_ms = static_cast<double>(w.front()) / 1e6;
    const double median_ms = static_cast<double>(w[w.size() / 2]) / 1e6;
    const double max_ms = static_cast<double>(w.back()) / 1e6;
    const double mem_mib = static_cast<double>(times[stage].bytes) / (1024.0 * 1024.0);
    io.out << std::left << std::setw(24) << stage << std::right << std::setw(12) << min_ms
           << std::setw(12) << median_ms << std::setw(12) << max_ms << std::setw(12) << mem_mib
           << "\n";
    stages.push_back({{"stage", stage},
                      {"runs", w.size()},
                      {"min_ms", min_ms},
                      {"median_ms", median_ms},
                      {"max_ms", max_ms},
                      {"bytes_owned_after", times[stage].bytes}});
  }
  io.out << std::defaultfloat << std::setprecision(6);

  if (auto json = args.value("json")) {
    nlohmann::ordered_json doc;
    doc["profile"] = gen->pipeline.profile().name;
    doc["grid_size"] = gen->params.grid.size;
    doc["seed"] = gen->seed;
    doc["repeat"] = repeat;
    doc["stages"] = std::move(stages);
    const std::string text =
        doc.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
    if (core::Status s = write_file(*json, text); !s.ok()) {
      return fail(io, s);
    }
  }
  return 0;
}

}  // namespace wg::cli
