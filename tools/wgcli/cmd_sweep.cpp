// wgcli sweep: many seeds, metrics table (plan section 6.7).

#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands.hpp"

namespace wg::cli {

namespace {

constexpr std::array kOptions = {
    OptionSpec{"seeds", "LIST", "Seeds such as 1..1000 or 1,5,9, required", OptionKind::value},
    OptionSpec{"metrics", "FILE", "Write one CSV row per seed", OptionKind::value},
    OptionSpec{"keep-failures", "DIR", "Write seed_<s>.json for every failed seed",
               OptionKind::value},
    kProfileOption,
    kParamsOption,
    kSizeOption,
    kThreadsOption,
    kDataDirOption,
    kQuietOption,
};

struct Row {
  std::uint64_t seed = 0;
  std::string status;
  std::uint32_t attempts = 0;
  std::size_t rejections = 0;
  std::string last_check;
  double wall_ms = 0.0;
  std::map<std::string, double> metrics;  ///< "<stage>.<metric>" of the final attempt
};

Row make_row(std::uint64_t seed, const pipeline::RunReport& report, std::string status) {
  Row row{.seed = seed,
          .status = std::move(status),
          .attempts = report.provenance.attempt + 1,
          .rejections = report.provenance.rejections.size(),
          .last_check = {},
          .wall_ms = 0.0,
          .metrics = {}};
  if (!report.provenance.rejections.empty()) {
    row.last_check = report.provenance.rejections.back().check_id;
  }
  for (const pipeline::StageRecord& r : report.stages) {
    row.wall_ms += static_cast<double>(r.metrics.wall_ns) / 1e6;
    if (row.status == "ok" && r.attempt == report.provenance.attempt) {
      for (const auto& [name, value] : r.metrics.values) {
        row.metrics.emplace(r.stage + "." + name, value);
      }
    }
  }
  return row;
}

std::string failure_json(std::uint64_t seed, const pipeline::FailureReport& failure) {
  nlohmann::ordered_json doc;
  doc["seed"] = seed;
  doc["status"] = core::to_string(failure.status.code);
  doc["message"] = failure.status.message;
  doc["profile"] = failure.report.provenance.profile;
  doc["attempts"] = failure.report.provenance.attempt + 1;
  nlohmann::ordered_json rejections = nlohmann::ordered_json::array();
  for (const core::RejectionRecord& r : failure.report.provenance.rejections) {
    rejections.push_back({{"attempt", r.attempt},
                          {"stage", r.stage},
                          {"check_id", r.check_id},
                          {"measured", r.measured},
                          {"required", r.required}});
  }
  doc["rejections"] = std::move(rejections);
  nlohmann::ordered_json stages = nlohmann::ordered_json::array();
  for (const pipeline::StageRecord& r : failure.report.stages) {
    stages.push_back({{"stage", r.stage},
                      {"attempt", r.attempt},
                      {"result", pipeline::to_string(r.kind)},
                      {"check_id", r.check_id}});
  }
  doc["stages"] = std::move(stages);
  return doc.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

std::string csv(const std::vector<Row>& rows) {
  std::set<std::string> columns;
  for (const Row& row : rows) {
    for (const auto& entry : row.metrics) {
      columns.insert(entry.first);
    }
  }
  std::ostringstream text;
  text << "seed,status,attempts,rejections,last_check,wall_ms";
  for (const std::string& c : columns) {
    text << "," << c;
  }
  text << "\n";
  for (const Row& row : rows) {
    text << row.seed << "," << row.status << "," << row.attempts << "," << row.rejections << ","
         << row.last_check << "," << row.wall_ms;
    for (const std::string& c : columns) {
      text << ",";
      if (auto it = row.metrics.find(c); it != row.metrics.end()) {
        text << it->second;
      }
    }
    text << "\n";
  }
  return text.str();
}

}  // namespace

const CommandSpec kSweepSpec{
    .name = "sweep",
    .summary = "Run many seeds and tabulate the outcomes",
    .positionals = "",
    .min_positionals = 0,
    .max_positionals = 0,
    .description =
        "Generates one world per seed without writing it and records the status, attempts,\n"
        "rejections and stage metrics of each. Failed seeds do not change the exit code: a\n"
        "sweep measures failure rates.",
    .options = kOptions,
};

int run_sweep(const ParsedArgs& args, const Io& io) {
  const std::optional<std::string> seeds_arg = args.value("seeds");
  if (!seeds_arg) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "sweep: option --seeds is required"));
  }
  auto seeds = parse_seed_list(*seeds_arg);
  if (!seeds) {
    return fail(io, seeds.error());
  }
  auto gen = make_generator(args, io);
  if (!gen) {
    return fail(io, gen.error());
  }
  const std::optional<std::string> keep = args.value("keep-failures");

  StreamLogSink log(io.err, core::LogLevel::error);
  std::vector<Row> rows;
  std::size_t failed = 0;
  std::uint64_t attempts = 0;
  for (const std::uint64_t seed : *seeds) {
    pipeline::RunResult result = gen->run(seed, log);
    if (result) {
      rows.push_back(make_row(seed, result->report, "ok"));
    } else {
      const pipeline::FailureReport& failure = result.error();
      if (failure.status.code != core::StatusCode::failed_precondition) {
        return fail(io, core::make_status(failure.status.code, "seed " + std::to_string(seed) +
                                                                   ": " + failure.status.message));
      }
      ++failed;
      rows.push_back(make_row(seed, failure.report, "failed"));
      if (keep) {
        const std::filesystem::path path =
            std::filesystem::path(*keep) / ("seed_" + std::to_string(seed) + ".json");
        if (core::Status s = write_file(path, failure_json(seed, failure)); !s.ok()) {
          return fail(io, s);
        }
      }
    }
    attempts += rows.back().attempts;
  }

  if (auto metrics = args.value("metrics")) {
    if (core::Status s = write_file(*metrics, csv(rows)); !s.ok()) {
      return fail(io, s);
    }
  }
  const auto n = static_cast<double>(rows.size());
  io.out << "seeds: " << rows.size() << "\n"
         << "ok: " << rows.size() - failed << "\n"
         << "failed: " << failed << "\n"
         << "mean attempts: " << static_cast<double>(attempts) / n << "\n";
  return 0;
}

}  // namespace wg::cli
