// wgcli hash: world, field and stage hashes for golden tests (plan sections 6.7 and 10.3).

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands.hpp"
#include "wg/io/world_file.hpp"
#include "world_hash.hpp"

namespace wg::cli {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::array kOptions = {
    OptionSpec{"per-stage", "", "Also hash the state after every stage (needs a generation)",
               OptionKind::flag},
    OptionSpec{"fields", "", "Also print the hash of every field", OptionKind::flag},
    OptionSpec{"seeds", "FILE", "Golden seed list: hash every case and seed of FILE",
               OptionKind::value},
    OptionSpec{"out", "FILE", "Write the hashes as JSON to FILE", OptionKind::value},
    kProfileOption,
    kParamsOption,
    kSeedOption,
    kSizeOption,
    kThreadsOption,
    kDataDirOption,
    kQuietOption,
};

/// Version of the seeds file and of the JSON written by --seeds --out.
constexpr std::int64_t kSeedsSchemaVersion = 1;

std::string dump(const Json& json) {
  return json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

void add_hashes(Json& doc, const WorldHashes& hashes, const std::vector<StageHash>* stages,
                bool fields) {
  doc["world"] = hex64(hashes.world);
  if (fields) {
    Json f = Json::object();
    for (const auto& [id, hash] : hashes.fields) {
      f[std::string(core::field_info(id).name)] = hex64(hash);
    }
    f["$layers"] = hex64(hashes.layers);
    doc["fields"] = std::move(f);
  }
  if (stages != nullptr) {
    Json s = Json::array();
    for (const StageHash& h : *stages) {
      s.push_back({{"stage", h.stage}, {"world", hex64(h.world)}});
    }
    doc["stages"] = std::move(s);
  }
}

void print_hashes(std::ostream& out, std::string_view prefix, const WorldHashes& hashes,
                  const std::vector<StageHash>* stages, bool fields) {
  out << prefix << "world " << hex64(hashes.world) << "\n";
  if (fields) {
    for (const auto& [id, hash] : hashes.fields) {
      out << prefix << "field " << core::field_info(id).name << " " << hex64(hash) << "\n";
    }
    out << prefix << "layers " << hex64(hashes.layers) << "\n";
  }
  if (stages != nullptr) {
    for (const StageHash& h : *stages) {
      out << prefix << "stage " << h.stage << " " << hex64(h.world) << "\n";
    }
  }
}

/// Outcome of hashing one generated world.
struct GeneratedHash {
  bool ok = false;
  core::Status status;  ///< why the run failed, if !ok
  std::uint32_t attempt = 0;
  WorldHashes hashes;
  std::vector<StageHash> stages;
};

core::Expected<GeneratedHash> hash_generated(Generator& gen, std::uint64_t seed, bool per_stage,
                                             core::LogSink& log) {
  StageHasher hasher;
  pipeline::RunResult result = gen.run(seed, log, per_stage ? &hasher : nullptr);
  GeneratedHash out;
  if (!result) {
    if (result.error().status.code != core::StatusCode::failed_precondition) {
      return core::make_error(result.error().status);
    }
    out.status = result.error().status;
    out.attempt = result.error().report.provenance.attempt;
    return out;
  }
  auto hashes = hash_world(result->state);
  if (!hashes) {
    return core::make_error(std::move(hashes.error()));
  }
  out.ok = true;
  out.attempt = result->state.provenance().attempt;
  out.hashes = std::move(*hashes);
  out.stages = hasher.stages_of(out.attempt);
  return out;
}

int finish(const ParsedArgs& args, const Io& io, const Json& doc) {
  if (auto out = args.value("out")) {
    if (core::Status s = write_file(*out, dump(doc)); !s.ok()) {
      return fail(io, s);
    }
  }
  return 0;
}

int hash_file(const ParsedArgs& args, const Io& io, const std::string& path) {
  if (args.has("per-stage") || args.has("seeds") || args.has("profile")) {
    return fail(io, core::make_status(core::StatusCode::invalid_argument,
                                      "hash: --per-stage, --seeds and --profile need a "
                                      "generation, not a world file"));
  }
  auto reader = io::WorldReader::open_file(path);
  if (!reader) {
    return fail(io, core::make_status(reader.error().code, path + ": " + reader.error().message));
  }
  auto hashes = hash_world_file(*reader);
  if (!hashes) {
    return fail(io, core::make_status(hashes.error().code, path + ": " + hashes.error().message));
  }
  const bool fields = args.has("fields");
  print_hashes(io.out, "", *hashes, nullptr, fields);
  Json doc = Json::object();
  add_hashes(doc, *hashes, nullptr, fields);
  return finish(args, io, doc);
}

int hash_generation(const ParsedArgs& args, const Io& io) {
  auto gen = make_generator(args, io);
  if (!gen) {
    return fail(io, gen.error());
  }
  StreamLogSink log(io.err, args.has("quiet") ? core::LogLevel::error : core::LogLevel::warning);
  const bool per_stage = args.has("per-stage");
  const bool fields = args.has("fields");
  auto h = hash_generated(*gen, gen->seed, per_stage, log);
  if (!h) {
    return fail(io, h.error());
  }
  if (!h->ok) {
    io.err << "wgcli: generation failed: " << core::to_string(h->status) << "\n";
    return static_cast<int>(ExitCode::generation_failed);
  }
  print_hashes(io.out, "", h->hashes, per_stage ? &h->stages : nullptr, fields);
  Json doc = Json::object();
  doc["profile"] = gen->pipeline.profile().name;
  doc["seed"] = gen->seed;
  doc["attempt"] = h->attempt;
  add_hashes(doc, h->hashes, per_stage ? &h->stages : nullptr, fields);
  return finish(args, io, doc);
}

/// A profile or params entry of the seeds file: a path relative to the file, else a name.
std::string relative_to(const std::filesystem::path& dir, const std::string& entry) {
  std::error_code ec;
  const std::filesystem::path p = dir / entry;
  return std::filesystem::is_regular_file(p, ec) ? p.string() : entry;
}

core::Status seeds_error(const std::string& path, const std::string& message) {
  return core::make_status(core::StatusCode::invalid_argument, path + ": " + message);
}

int hash_seeds(const ParsedArgs& args, const Io& io, const std::string& path) {
  auto text = read_file(path);
  if (!text) {
    return fail(io, text.error());
  }
  const Json doc = Json::parse(*text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    return fail(io, core::make_status(core::StatusCode::parse_error, path + ": not a JSON object"));
  }
  const auto version = doc.find("schema_version");
  if (version == doc.end() || !version->is_number_integer()) {
    return fail(io, seeds_error(path, "$.schema_version: missing or not an integer"));
  }
  if (version->get<std::int64_t>() > kSeedsSchemaVersion) {
    return fail(io, core::make_status(core::StatusCode::unsupported_version,
                                      path + ": schema_version " +
                                          std::to_string(version->get<std::int64_t>()) +
                                          " is newer than 1"));
  }
  const auto cases = doc.find("cases");
  if (cases == doc.end() || !cases->is_array()) {
    return fail(io, seeds_error(path, "$.cases: missing or not an array"));
  }

  const std::filesystem::path dir = std::filesystem::path(path).parent_path();
  const bool per_stage = args.has("per-stage");
  const bool fields = args.has("fields");
  StreamLogSink log(io.err, core::LogLevel::error);
  Json out_cases = Json::array();
  for (std::size_t c = 0; c < cases->size(); ++c) {
    const Json& item = (*cases)[c];
    const std::string at = "$.cases[" + std::to_string(c) + "]";
    const auto name = item.find("name");
    const auto profile = item.find("profile");
    const auto params = item.find("params");
    const auto size = item.find("size");
    const auto seeds = item.find("seeds");
    if (!item.is_object() || name == item.end() || !name->is_string() || profile == item.end() ||
        !profile->is_string() || (params != item.end() && !params->is_string()) ||
        (size != item.end() && !size->is_number_integer()) || seeds == item.end() ||
        !seeds->is_array()) {
      return fail(io, seeds_error(path, at + ": needs name, profile and seeds (params and "
                                             "size optional) of the right types"));
    }
    GeneratorOptions options = generator_options(args);
    options.profile = relative_to(dir, profile->get<std::string>());
    options.params = params == item.end() ? std::optional<std::string>()
                                          : relative_to(dir, params->get<std::string>());
    options.size = size == item.end() ? options.size : std::to_string(size->get<std::int64_t>());
    options.quiet = true;
    auto gen = make_generator(options, io);
    if (!gen) {
      std::string message = path;
      message += ": " + at;
      message += ": " + gen.error().message;
      return fail(io, core::make_status(gen.error().code, message));
    }
    Json results = Json::array();
    for (const Json& s : *seeds) {
      if (!s.is_number_unsigned()) {
        return fail(io, seeds_error(path, at + ".seeds: seeds must be unsigned integers"));
      }
      const auto seed = s.get<std::uint64_t>();
      auto h = hash_generated(*gen, seed, per_stage, log);
      if (!h) {
        return fail(io, h.error());
      }
      const std::string prefix = name->get<std::string>() + " " + std::to_string(seed) + " ";
      Json r = Json::object();
      r["seed"] = seed;
      r["attempt"] = h->attempt;
      if (h->ok) {
        r["status"] = "ok";
        add_hashes(r, h->hashes, per_stage ? &h->stages : nullptr, fields);
        print_hashes(io.out, prefix, h->hashes, per_stage ? &h->stages : nullptr, fields);
      } else {
        r["status"] = "failed";
        io.out << prefix << "failed\n";
      }
      results.push_back(std::move(r));
    }
    out_cases.push_back({{"name", *name}, {"results", std::move(results)}});
  }
  Json out = Json::object();
  out["schema_version"] = kSeedsSchemaVersion;
  out["cases"] = std::move(out_cases);
  return finish(args, io, out);
}

}  // namespace

const CommandSpec kHashSpec{
    .name = "hash",
    .summary = "World, field and stage hashes for golden tests",
    .positionals = "[WORLD]",
    .min_positionals = 0,
    .max_positionals = 1,
    .description =
        "Hashes a world file, or generates a world (--profile ...) and hashes it, or hashes\n"
        "every case and seed of a golden seed list (--seeds). The world hash covers the grid\n"
        "size, every field's raw bytes and the vector layers, never metrics or versions\n"
        "(ADR 0008), so it is the same for a generated world and its file.",
    .options = kOptions,
};

int run_hash(const ParsedArgs& args, const Io& io) {
  if (!args.positionals().empty()) {
    return hash_file(args, io, args.positionals()[0]);
  }
  if (auto seeds = args.value("seeds")) {
    return hash_seeds(args, io, *seeds);
  }
  return hash_generation(args, io);
}

}  // namespace wg::cli
