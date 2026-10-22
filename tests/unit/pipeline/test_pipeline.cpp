#include "wg/pipeline/pipeline.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/mock_stages.hpp"
#include "support/recording_sinks.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/core/version.hpp"
#include "wg/params/hash.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::params::GenParams;
using wg::pipeline::FieldSet;
using wg::pipeline::LayerId;
using wg::pipeline::Pipeline;
using wg::pipeline::Profile;
using wg::pipeline::RunContext;
using wg::pipeline::RunInputs;
using wg::pipeline::RunResult;
using wg::pipeline::SnapshotInfo;
using wg::pipeline::StageRegistry;
using wg::pipeline::StageResult;
using wg::test::mock;
using wg::test::MockScript;

namespace {

constexpr std::uint64_t kSeed = 42;

GenParams small_params() {
  GenParams p;
  p.grid.size = 9;
  p.checks.max_attempts = 5;
  return p;
}

// m01 writes latitude, m02 reads it and writes elevation and peaks, m03 reads both.
struct MockProfile {
  std::shared_ptr<MockScript> seed = mock("m01_seed", {}, {FieldId::latitude});
  std::shared_ptr<MockScript> shape =
      mock("m02_shape", {FieldId::latitude}, {FieldId::elevation, LayerId::peaks});
  std::shared_ptr<MockScript> verify =
      mock("m03_verify", {FieldId::elevation, LayerId::peaks}, {FieldId::biome});
  StageRegistry registry;
  Profile profile{.name = "mock", .stages = {"m01_seed", "m02_shape", "m03_verify"}};

  MockProfile() {
    REQUIRE(wg::test::add_mock(registry, seed).ok());
    REQUIRE(wg::test::add_mock(registry, shape).ok());
    REQUIRE(wg::test::add_mock(registry, verify).ok());
  }

  [[nodiscard]] Pipeline pipeline() const {
    auto p = Pipeline::create(profile, registry);
    REQUIRE(p);
    return std::move(*p);
  }
};

class RecordingHook final : public wg::pipeline::SnapshotHook {
 public:
  struct Entry {
    std::string stage;
    std::size_t index;
    std::uint32_t attempt;
    bool final;
    std::string last_completed;
  };

  wg::core::Status on_stage(const wg::core::WorldState& state,
                            const SnapshotInfo& info) noexcept override {
    entries.push_back(
        Entry{.stage = std::string(info.stage),
              .index = info.stage_index,
              .attempt = info.attempt,
              .final = info.final,
              .last_completed = std::string(state.provenance().last_completed_stage())});
    if (fail_at == entries.size()) {
      return wg::core::make_status(StatusCode::io_error, "disk full");
    }
    return {};
  }

  std::vector<Entry> entries;
  std::size_t fail_at = 0;  ///< 1-based call that fails; 0: none
};

}  // namespace

// --- Pipeline::create: R2 -------------------------------------------------------------------

TEST_CASE("pipeline.pipeline create refuses a mock profile with a missing producer") {
  MockProfile m;
  m.profile.stages = {"m02_shape", "m01_seed", "m03_verify"};
  auto p = Pipeline::create(m.profile, m.registry);
  REQUIRE_FALSE(p);
  CHECK(p.error().code == StatusCode::failed_precondition);
  CHECK(p.error().message ==
        "profile mock: stage m02_shape reads latitude, which no earlier stage produces");

  m.profile.stages = {"m01_seed", "m03_verify"};
  p = Pipeline::create(m.profile, m.registry);
  REQUIRE_FALSE(p);
  CHECK(p.error().message ==
        "profile mock: stage m03_verify reads elevation, peaks, which no earlier stage produces");
}

TEST_CASE("pipeline.pipeline create accepts a causally ordered profile") {
  MockProfile m;
  auto p = Pipeline::create(m.profile, m.registry);
  REQUIRE(p);
  CHECK(p->profile() == m.profile);
  CHECK(p->provided().empty());
  CHECK(p->outputs() ==
        FieldSet{FieldId::latitude, FieldId::elevation, FieldId::biome, LayerId::peaks});
}

TEST_CASE("pipeline.pipeline create counts provided inputs as produced (R4)") {
  MockProfile m;
  const Profile alone{.name = "alone", .stages = {"m03_verify"}};
  CHECK(Pipeline::create(alone, m.registry).error().code == StatusCode::failed_precondition);
  CHECK(Pipeline::create(alone, m.registry, {FieldId::elevation}).error().code ==
        StatusCode::failed_precondition);
  auto p = Pipeline::create(alone, m.registry, {FieldId::elevation, LayerId::peaks});
  REQUIRE(p);
  CHECK(p->provided() == FieldSet{FieldId::elevation, LayerId::peaks});
}

TEST_CASE("pipeline.pipeline create refuses empty profiles, unknown and repeated stages") {
  MockProfile m;
  CHECK(Pipeline::create(Profile{.name = "empty", .stages = {}}, m.registry).error().code ==
        StatusCode::invalid_argument);

  auto unknown = Pipeline::create(Profile{.name = "x", .stages = {"m01_seed", "nope"}}, m.registry);
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().code == StatusCode::not_found);
  CHECK(unknown.error().message == "profile x: stage nope is not registered");

  auto twice =
      Pipeline::create(Profile{.name = "x", .stages = {"m01_seed", "m01_seed"}}, m.registry);
  REQUIRE_FALSE(twice);
  CHECK(twice.error().code == StatusCode::invalid_argument);
  CHECK(twice.error().message == "profile x: stage m01_seed is listed twice");
}

// --- Pipeline::run ----------------------------------------------------------------------------

TEST_CASE("pipeline.pipeline run produces the world and its provenance") {
  MockProfile m;
  Pipeline p = m.pipeline();
  wg::core::SerialExecutor exec;
  wg::test::RecordingLogSink log;
  wg::test::RecordingProgressSink progress;
  const GenParams params = small_params();

  RunResult r = p.run(params, kSeed, RunContext{.exec = exec, .log = log, .progress = progress});
  REQUIRE(r);
  const wg::core::Provenance& prov = r->report.provenance;
  CHECK(prov.profile == "mock");
  CHECK(prov.seed == kSeed);
  CHECK(prov.params_hash == wg::params::params_hash(params));
  CHECK(prov.library_version.major == wg::core::version().major);
  CHECK(prov.attempt == 0);
  CHECK(prov.rejections.empty());
  CHECK(prov.completed_stages == std::vector<std::string>{"m01_seed", "m02_shape", "m03_verify"});
  CHECK(r->state.provenance().completed_stages == prov.completed_stages);
  CHECK(r->state.grid().n == 9);
  CHECK(r->state.has(FieldId::latitude));
  CHECK(r->state.has(FieldId::elevation));
  CHECK(r->state.has(FieldId::biome));
  CHECK(r->state.layers().peaks.size() == 1);

  for (const auto& s : {m.seed, m.shape, m.verify}) {
    REQUIRE(s->calls.size() == 1);
    CHECK(s->calls[0].key == wg::core::stage_key(kSeed, s->id, 0));
    CHECK(s->calls[0].params == &params);
  }

  std::vector<double> fractions;
  for (const auto& e : progress.entries()) {
    if (e.scope == "pipeline") {
      fractions.push_back(e.fraction);
    }
  }
  REQUIRE(fractions.size() == 4);
  CHECK(fractions.front() == 0.0);
  CHECK(fractions.back() == 1.0);
  CHECK(std::is_sorted(fractions.begin(), fractions.end()));
  for (const auto& e : log.entries()) {
    CHECK(e.level == wg::core::LogLevel::debug);
  }
}

TEST_CASE("pipeline.pipeline run restarts after a scripted reject with a new key, same params") {
  MockProfile m;
  m.shape->reject_on = {0};
  m.shape->check_id = "PEAK_NUMBER_MIN";
  m.shape->measured = 2.0;
  m.shape->required = 5.0;
  Pipeline p = m.pipeline();
  wg::core::SerialExecutor exec;
  wg::test::RecordingLogSink log;
  const GenParams params = small_params();

  RunResult r = p.run(params, kSeed, RunContext{.exec = exec, .log = log});
  REQUIRE(r);
  const wg::core::Provenance& prov = r->report.provenance;
  CHECK(prov.attempt == 1);
  REQUIRE(prov.rejections.size() == 1);
  const wg::core::RejectionRecord& rej = prov.rejections[0];
  CHECK(rej.attempt == 0);
  CHECK(rej.stage == "m02_shape");
  CHECK(rej.check_id == "PEAK_NUMBER_MIN");
  CHECK(rej.measured == 2.0);
  CHECK(rej.required == 5.0);
  CHECK(r->state.provenance().rejections.size() == 1);

  // Stage k = 1 ran in attempts 0 and 1 with the keys of those attempts and the same params.
  REQUIRE(m.shape->calls.size() == 2);
  CHECK(m.shape->calls[0].key == wg::core::stage_key(kSeed, "m02_shape", 0));
  CHECK(m.shape->calls[1].key == wg::core::stage_key(kSeed, "m02_shape", 1));
  CHECK(m.shape->calls[0].key != m.shape->calls[1].key);
  CHECK(m.shape->calls[0].params == &params);
  CHECK(m.shape->calls[1].params == &params);
  // Stage 0 ran again on a fresh state; stage 2 ran only in the successful attempt.
  REQUIRE(m.seed->calls.size() == 2);
  CHECK(m.seed->calls[1].attempt == 1);
  CHECK_FALSE(m.seed->calls[1].outputs_present);
  REQUIRE(m.verify->calls.size() == 1);
  CHECK(m.verify->calls[0].attempt == 1);
  CHECK(r->state.layers().peaks.size() == 1);

  const auto entries = log.entries();
  CHECK(std::any_of(entries.begin(), entries.end(), [](const auto& e) {
    return e.level == wg::core::LogLevel::warning &&
           e.message ==
               "attempt 0: stage m02_shape rejected PEAK_NUMBER_MIN (measured 2, "
               "required 5)";
  }));
}

TEST_CASE("pipeline.pipeline run reports every rejection when attempts are exhausted") {
  MockProfile m;
  m.verify->reject_always = true;
  Pipeline p = m.pipeline();
  wg::core::SerialExecutor exec;
  GenParams params = small_params();
  params.checks.max_attempts = 3;

  RunResult r = p.run(params, kSeed, RunContext{.exec = exec});
  REQUIRE_FALSE(r);
  CHECK(r.error().status.code == StatusCode::failed_precondition);
  CHECK(r.error().status.message ==
        "all 3 attempts were rejected; last: stage m03_verify, check MOCK_CHECK (measured 1, "
        "required 2)");
  const auto& rejections = r.error().report.provenance.rejections;
  REQUIRE(rejections.size() == 3);
  for (std::uint32_t a = 0; a < 3; ++a) {
    CHECK(rejections[a].attempt == a);
    CHECK(rejections[a].stage == "m03_verify");
  }
  CHECK(r.error().report.stages.size() == 9);
  CHECK(m.verify->calls.size() == 3);
}

TEST_CASE("pipeline.pipeline run repairs before it rejects") {
  MockProfile m;
  m.shape->reject_always = true;
  m.shape->repairable = true;
  Pipeline p = m.pipeline();
  wg::core::SerialExecutor exec;
  wg::test::RecordingLogSink log;

  RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .log = log});
  REQUIRE(r);
  CHECK(r->report.provenance.attempt == 0);
  CHECK(r->report.provenance.rejections.empty());
  CHECK(m.shape->repairs == 1);
  const auto& rec = r->report.stages[1];
  CHECK(rec.stage == "m02_shape");
  CHECK(rec.kind == StageResult::Kind::repaired);
  CHECK(rec.check_id == "MOCK_CHECK");
  CHECK(rec.metrics.value("cells") == 81.0);
  CHECK(rec.metrics.value("repaired_cells") == 3.0);
  const auto entries = log.entries();
  CHECK(std::any_of(entries.begin(), entries.end(), [](const auto& e) {
    return e.level == wg::core::LogLevel::warning &&
           e.message.find("stage m02_shape repaired MOCK_CHECK") != std::string::npos;
  }));
}

TEST_CASE("pipeline.pipeline run returns cancelled") {
  MockProfile m;
  wg::core::CancelFlag cancel;
  wg::core::SerialExecutor exec;

  SUBCASE("before the first stage") {
    Pipeline p = m.pipeline();
    cancel.request();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .cancel = cancel});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::cancelled);
    CHECK(m.seed->calls.empty());
    CHECK(r.error().report.stages.empty());
  }
  SUBCASE("requested while a stage runs: the next stage does not start") {
    m.shape->cancel_after_run = &cancel;
    Pipeline p = m.pipeline();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .cancel = cancel});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::cancelled);
    CHECK(r.error().status.message == "cancelled before stage m03_verify of attempt 0");
    CHECK(m.verify->calls.empty());
    CHECK(r.error().report.provenance.completed_stages ==
          std::vector<std::string>{"m01_seed", "m02_shape"});
  }
  SUBCASE("inside a kernel chunk") {
    m.seed->cancel_in_kernel = &cancel;
    Pipeline p = m.pipeline();
    auto pool = wg::core::ThreadPool::create(4);
    REQUIRE(pool);
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = **pool, .cancel = cancel});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::cancelled);
    CHECK(r.error().status.message.rfind("stage m01_seed: ", 0) == 0);
    CHECK(m.shape->calls.empty());
  }
}

TEST_CASE("pipeline.pipeline run reports metrics for every stage of every attempt") {
  MockProfile m;
  m.verify->reject_on = {0};
  Pipeline p = m.pipeline();
  wg::core::SerialExecutor exec;
  wg::test::CountingResource memory;

  RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .memory = &memory});
  REQUIRE(r);
  const auto& stages = r->report.stages;
  REQUIRE(stages.size() == 6);
  const std::vector<std::string> ids = {"m01_seed", "m02_shape", "m03_verify"};
  for (std::size_t i = 0; i < stages.size(); ++i) {
    CAPTURE(i);
    CHECK(stages[i].stage == ids[i % 3]);
    CHECK(stages[i].attempt == i / 3);
    CHECK(stages[i].metrics.wall_ns >= 0);
    CHECK(stages[i].metrics.bytes_owned_after > 0);
    CHECK(stages[i].metrics.value("cells") == 81.0);
  }
  CHECK(stages[2].kind == StageResult::Kind::reject);
  CHECK(stages[2].check_id == "MOCK_CHECK");
  CHECK(stages[5].kind == StageResult::Kind::ok);
  // latitude (f32) + elevation (f32) + biome (u16) on 81 cells, from the injected resource.
  CHECK(stages[5].metrics.bytes_owned_after == r->state.bytes_owned());
  CHECK(memory.bytes_outstanding == r->state.bytes_owned());
}

TEST_CASE("pipeline.pipeline run calls the snapshot hook after every completed stage") {
  MockProfile m;
  m.verify->reject_on = {0};
  wg::core::SerialExecutor exec;
  RecordingHook hook;

  SUBCASE("every stage, rejected attempts included") {
    Pipeline p = m.pipeline();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .snapshot = &hook});
    REQUIRE(r);
    REQUIRE(hook.entries.size() == 5);  // attempt 0: m01, m02 (m03 rejects); attempt 1: all
    CHECK(hook.entries[0].stage == "m01_seed");
    CHECK(hook.entries[0].attempt == 0);
    CHECK(hook.entries[0].last_completed == "m01_seed");
    CHECK(hook.entries[2].stage == "m01_seed");
    CHECK(hook.entries[2].attempt == 1);
    CHECK(hook.entries[4].stage == "m03_verify");
    CHECK(hook.entries[4].index == 2);
    CHECK(hook.entries[4].last_completed == "m03_verify");
    for (std::size_t i = 0; i < hook.entries.size(); ++i) {
      CHECK(hook.entries[i].final == (i == 4));
    }
  }
  SUBCASE("a failing hook stops the run") {
    hook.fail_at = 2;
    Pipeline p = m.pipeline();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec, .snapshot = &hook});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::io_error);
    CHECK(r.error().status.message == "snapshot after stage m02_shape: disk full");
    CHECK(m.verify->calls.empty());
  }
}

TEST_CASE("pipeline.pipeline run stops on stage errors and contract violations") {
  MockProfile m;
  wg::core::SerialExecutor exec;

  SUBCASE("stage error") {
    m.shape->error = wg::core::make_status(StatusCode::out_of_range, "too steep");
    Pipeline p = m.pipeline();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::out_of_range);
    CHECK(r.error().status.message == "stage m02_shape: too steep");
    CHECK(r.error().report.stages.back().kind == StageResult::Kind::error);
  }
  SUBCASE("declared output not written") {
    m.shape->skip_outputs = true;
    Pipeline p = m.pipeline();
    RunResult r = p.run(small_params(), kSeed, RunContext{.exec = exec});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::internal);
    CHECK(r.error().status.message == "stage m02_shape did not write its outputs elevation");
  }
  SUBCASE("max_attempts below 1") {
    Pipeline p = m.pipeline();
    GenParams params = small_params();
    params.checks.max_attempts = 0;
    CHECK(p.run(params, kSeed, RunContext{.exec = exec}).error().status.code ==
          StatusCode::invalid_argument);
  }
  SUBCASE("invalid grid") {
    Pipeline p = m.pipeline();
    GenParams params = small_params();
    params.grid.size = 10;
    CHECK(p.run(params, kSeed, RunContext{.exec = exec}).error().status.code ==
          StatusCode::invalid_argument);
  }
}

TEST_CASE("pipeline.pipeline run starts every attempt from the initial state (R4)") {
  MockProfile m;
  m.verify->reject_on = {0};
  const Profile alone{.name = "alone", .stages = {"m03_verify"}};
  auto created = Pipeline::create(alone, m.registry, {FieldId::elevation, LayerId::peaks});
  REQUIRE(created);
  Pipeline p = std::move(*created);
  wg::core::SerialExecutor exec;
  const GenParams params = small_params();
  wg::params::DataTables tables;

  auto initial = wg::core::WorldState::create(
      wg::core::Grid{.n = 9, .dx_m = params.grid.dx_m, .hu_m = params.grid.hu_m});
  REQUIRE(initial);

  SUBCASE("missing provided field") {
    RunResult r = p.run(params, kSeed, RunContext{.exec = exec}, RunInputs{.initial = &*initial});
    REQUIRE_FALSE(r);
    CHECK(r.error().status.code == StatusCode::invalid_argument);
    CHECK(r.error().status.message == "the initial state lacks provided fields: elevation");
  }
  SUBCASE("no initial state") {
    CHECK(p.run(params, kSeed, RunContext{.exec = exec}).error().status.code ==
          StatusCode::invalid_argument);
  }
  SUBCASE("grid mismatch") {
    REQUIRE(initial->write<float>(FieldId::elevation));
    GenParams other = params;
    other.grid.size = 17;
    CHECK(p.run(other, kSeed, RunContext{.exec = exec}, RunInputs{.initial = &*initial})
              .error()
              .status.code == StatusCode::invalid_argument);
  }
  SUBCASE("copied into every attempt, never modified") {
    auto elevation = initial->write<float>(FieldId::elevation);
    REQUIRE(elevation);
    (*elevation)[40] = 123.0F;
    initial->layers().peaks.push_back(wg::core::Peak{.cell = 40});

    RunResult r = p.run(params, kSeed, RunContext{.exec = exec},
                        RunInputs{.tables = &tables, .initial = &*initial});
    REQUIRE(r);
    CHECK(r->report.provenance.attempt == 1);
    REQUIRE(m.verify->calls.size() == 2);
    CHECK(m.verify->calls[1].tables == &tables);
    auto out = r->state.read<float>(FieldId::elevation);
    REQUIRE(out);
    CHECK((*out)[40] == 123.0F);
    CHECK(r->state.layers().peaks.size() == 1);
    CHECK(initial->has(FieldId::elevation));
    CHECK_FALSE(initial->has(FieldId::biome));
    CHECK(initial->provenance().completed_stages.empty());
  }
}

TEST_CASE("pipeline.pipeline run gives identical fields for 1 and 4 threads (R1)") {
  MockProfile m;
  m.shape->reject_on = {0};
  const GenParams params = small_params();

  wg::core::SerialExecutor serial;
  Pipeline p1 = m.pipeline();
  RunResult a = p1.run(params, kSeed, RunContext{.exec = serial});
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  Pipeline p4 = m.pipeline();
  RunResult b = p4.run(params, kSeed, RunContext{.exec = **pool});
  REQUIRE(a);
  REQUIRE(b);

  const auto ea = a->state.read<float>(FieldId::elevation);
  const auto eb = b->state.read<float>(FieldId::elevation);
  REQUIRE(ea);
  REQUIRE(eb);
  CHECK(std::equal(ea->data.begin(), ea->data.end(), eb->data.begin(), eb->data.end()));
  const auto ba = a->state.read<std::uint16_t>(FieldId::biome);
  const auto bb = b->state.read<std::uint16_t>(FieldId::biome);
  REQUIRE(ba);
  REQUIRE(bb);
  CHECK(std::equal(ba->data.begin(), ba->data.end(), bb->data.begin(), bb->data.end()));

  // A different seed gives a different world.
  Pipeline p2 = m.pipeline();
  RunResult c = p2.run(params, kSeed + 1, RunContext{.exec = serial});
  REQUIRE(c);
  const auto ec = c->state.read<float>(FieldId::elevation);
  REQUIRE(ec);
  CHECK_FALSE(std::equal(ea->data.begin(), ea->data.end(), ec->data.begin(), ec->data.end()));
}
