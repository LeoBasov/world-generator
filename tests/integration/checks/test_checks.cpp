// A22 acceptance tests over every R11 check of the classic profile: each check fails just below
// and passes just above its threshold; contradictory parameters end in a complete failure report;
// measured attempts are consistent with (prod_i p_i)^-1.

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/stage_run.hpp"
#include "support/stats.hpp"
#include "wg/core/rng.hpp"
#include "wg/params/validation.hpp"
#include "wg/pipeline/attempts.hpp"

using wg::core::StatusCode;
using wg::params::ChecksParams;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::uint64_t kSeed = 9;
constexpr std::int32_t kHuge = 1000000;
// Pass rates of the two coin stages.
constexpr double kPass1 = 0.6;
constexpr double kPass2 = 0.7;

const std::vector<std::string> kClassic = {
    "c01_allocate", "c02_seed",    "c03_fit",    "c04_vegetation", "c05_shape", "c06_rivers",
    "c07_smooth",   "c08_climate", "c09_biomes", "c10_subsurface", "c11_verify"};

GenParams small_params() {
  GenParams p;
  p.grid.size = 33;
  p.erosion.erosion_cycle_count = 4;
  p.rivers.small_ocean_min_area = 4;
  p.elevation_post.final_iterations = 5;
  return p;
}

// One threshold: the stage that checks it, its check id and how to set it.
struct Threshold {
  std::string stage;
  std::string check_id;
  std::function<void(ChecksParams&, std::int32_t)> set;
};

// Runs the classic stages before t.stage (with the data tables), then t.stage alone without its
// repair hook, as the runner's first call in attempt 0 would.
StageResult run_check(const Threshold& t, std::int32_t value) {
  GenParams p = small_params();
  t.set(p.checks, value);
  std::vector<std::string> before;
  for (const std::string& id : kClassic) {
    if (id == t.stage) {
      break;
    }
    before.push_back(id);
  }
  wg::core::SerialExecutor exec;
  wg::core::WorldState state = wg::test::run_classic(before, p, kSeed, exec);
  auto stage = wg::test::make_classic_stage(t.stage);
  return wg::test::run_stage(*stage, state, p, kSeed, exec, wg::pipeline::never_cancelled(),
                             &wg::test::repo_tables());
}

// A minimum: a huge threshold reports the measured count m; m passes and m + 1 rejects.
void check_minimum(const Threshold& t) {
  CAPTURE(t.check_id);
  const StageResult probe = run_check(t, kHuge);
  REQUIRE(probe.kind == StageResult::Kind::reject);
  REQUIRE(probe.check_id == t.check_id);
  CHECK(probe.required == static_cast<double>(kHuge));
  const auto m = static_cast<std::int32_t>(probe.measured);
  CHECK(static_cast<double>(m) == probe.measured);

  CHECK(run_check(t, m).kind == StageResult::Kind::ok);
  const StageResult above = run_check(t, m + 1);
  CHECK(above.kind == StageResult::Kind::reject);
  CHECK(above.check_id == t.check_id);
  CHECK(above.measured == static_cast<double>(m));
  CHECK(above.required == static_cast<double>(m + 1));
}

// Sets entry k of the array member of ChecksParams.
template <std::size_t N>
std::function<void(ChecksParams&, std::int32_t)> entry(std::array<std::int32_t, N> ChecksParams::*a,
                                                       std::size_t k) {
  return [a, k](ChecksParams& c, std::int32_t v) { (c.*a)[k] = v; };
}

std::function<void(ChecksParams&, std::int32_t)> region_entry(
    std::array<std::int32_t, 3> wg::params::ChecksRegionCountsParams::*a, std::size_t k) {
  return [a, k](ChecksParams& c, std::int32_t v) { (c.region_counts.*a)[k] = v; };
}

struct RegionType {
  std::string_view name;
  std::array<std::int32_t, 3> wg::params::ChecksRegionCountsParams::*member;
};

constexpr std::array<RegionType, 9> kRegionTypes = {{
    {"SWAMP", &wg::params::ChecksRegionCountsParams::swamp},
    {"DESERT", &wg::params::ChecksRegionCountsParams::desert},
    {"FOREST", &wg::params::ChecksRegionCountsParams::forest},
    {"MOUNTAINS", &wg::params::ChecksRegionCountsParams::mountains},
    {"OCEAN", &wg::params::ChecksRegionCountsParams::ocean},
    {"GLACIER", &wg::params::ChecksRegionCountsParams::glacier},
    {"TUNDRA", &wg::params::ChecksRegionCountsParams::tundra},
    {"GRASSLAND", &wg::params::ChecksRegionCountsParams::grassland},
    {"HILLS", &wg::params::ChecksRegionCountsParams::hills},
}};

// Rejects when u = uniform01(key, 0, 0) >= pass_rate: passes with probability pass_rate per
// attempt, independently per stage and attempt (R1 keys).
class CoinStage final : public wg::pipeline::Stage {
 public:
  CoinStage(std::string id, double pass_rate) : id_(std::move(id)), pass_rate_(pass_rate) {}
  [[nodiscard]] std::string_view id() const noexcept override { return id_; }
  [[nodiscard]] wg::pipeline::FieldSet inputs() const noexcept override { return {}; }
  [[nodiscard]] wg::pipeline::FieldSet outputs() const noexcept override { return {}; }
  StageResult run(wg::pipeline::StageContext& ctx) noexcept override {
    const double u = wg::core::uniform01(ctx.key, 0, 0);
    return u < pass_rate_ ? StageResult::ok() : StageResult::reject("COIN", u, pass_rate_);
  }

 private:
  std::string id_;
  double pass_rate_;
};

}  // namespace

TEST_CASE("checks.c03_fit edge oceans and peaks fail just above the measured count") {
  check_minimum({"c03_fit", "COMPLETE_OCEAN_EDGE_MIN",
                 [](ChecksParams& c, std::int32_t v) { c.complete_ocean_edge_min = v; }});
  check_minimum({"c03_fit", "PARTIAL_OCEAN_EDGE_MIN",
                 [](ChecksParams& c, std::int32_t v) { c.partial_ocean_edge_min = v; }});
  check_minimum({"c03_fit", "PEAK_NUMBER_MIN",
                 [](ChecksParams& c, std::int32_t v) { c.peak_number_min = v; }});
}

TEST_CASE("checks.c03_fit every *_RANGES entry fails just above the measured count") {
  struct Ranges {
    std::string_view name;
    std::array<std::int32_t, 3> ChecksParams::*member;
  };
  const std::array<Ranges, 5> ranges = {{
      {"ELEVATION_RANGES", &ChecksParams::elevation_ranges},
      {"RAIN_RANGES", &ChecksParams::rain_ranges},
      {"DRAINAGE_RANGES", &ChecksParams::drainage_ranges},
      {"VOLCANISM_RANGES", &ChecksParams::volcanism_ranges},
      {"SAVAGERY_RANGES", &ChecksParams::savagery_ranges},
  }};
  for (const Ranges& r : ranges) {
    for (std::size_t k = 0; k < 3; ++k) {
      check_minimum(
          {"c03_fit", std::string(r.name) + "[" + std::to_string(k) + "]", entry(r.member, k)});
    }
  }
}

TEST_CASE("checks.c04_vegetation initial REGION_COUNTS fail just above the measured count") {
  for (const RegionType& t : kRegionTypes) {
    for (std::size_t k = 0; k < 2; ++k) {
      check_minimum({"c04_vegetation",
                     "REGION_COUNTS." + std::string(t.name) + "[" + std::to_string(k) + "]",
                     region_entry(t.member, k)});
    }
  }
}

TEST_CASE("checks.c05_shape and c06_rivers VOLCANO_MIN and RIVER_MINS") {
  check_minimum(
      {"c05_shape", "VOLCANO_MIN", [](ChecksParams& c, std::int32_t v) { c.volcano_min = v; }});
  check_minimum({"c06_rivers", "RIVER_MINS[0]", entry(&ChecksParams::river_mins, 0)});
  check_minimum({"c06_rivers", "RIVER_MINS[1]", entry(&ChecksParams::river_mins, 1)});
}

TEST_CASE("checks.c11_verify final REGION_COUNTS and SUBREGION_MAX") {
  for (const RegionType& t : kRegionTypes) {
    check_minimum(
        {"c11_verify", "REGION_COUNTS." + std::string(t.name) + "[2]", region_entry(t.member, 2)});
  }
  // A maximum: 0 reports the region count r; r passes and r - 1 rejects.
  const Threshold t{"c11_verify", "SUBREGION_MAX",
                    [](ChecksParams& c, std::int32_t v) { c.subregion_max = v; }};
  const StageResult probe = run_check(t, 0);
  REQUIRE(probe.kind == StageResult::Kind::reject);
  REQUIRE(probe.check_id == "SUBREGION_MAX");
  const auto r = static_cast<std::int32_t>(probe.measured);
  REQUIRE(r > 1);
  CHECK(run_check(t, r).kind == StageResult::Kind::ok);
  const StageResult below = run_check(t, r - 1);
  CHECK(below.kind == StageResult::Kind::reject);
  CHECK(below.measured == static_cast<double>(r));
  CHECK(below.required == static_cast<double>(r - 1));
}

TEST_CASE("checks.contradictory parameters end in a complete failure report") {
  const wg::pipeline::StageRegistry registry = wg::test::classic_registry();
  auto profile = wg::pipeline::load_profile_file(
      wg::test::source_path("tests/fixtures/profiles/classic_verify.json"));
  REQUIRE(profile);
  REQUIRE(profile->stages == kClassic);
  auto pipeline = wg::pipeline::Pipeline::create(*profile, registry);
  REQUIRE(pipeline);
  wg::core::SerialExecutor exec;
  const wg::pipeline::RunInputs inputs{.tables = &wg::test::repo_tables(), .initial = nullptr};

  SUBCASE("an early check no attempt can pass") {
    // Rainfall clamped to [50, 100] has no low squares; validation warns beforehand.
    GenParams p = small_params();
    p.fields.rainfall.min = 50.0;
    p.checks.rain_ranges = {1, 0, 0};
    p.checks.max_attempts = 4;
    const std::vector<wg::params::ValidationIssue> issues = wg::params::validate(p);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == wg::params::Severity::warning);
    CHECK(issues[0].path == "$.checks.RAIN_RANGES[0]");

    auto run = pipeline->run(p, kSeed, wg::pipeline::RunContext{.exec = exec}, inputs);
    REQUIRE_FALSE(run);
    const wg::pipeline::FailureReport& failure = run.error();
    CHECK(failure.status.code == StatusCode::failed_precondition);
    const wg::core::Provenance& prov = failure.report.provenance;
    REQUIRE(prov.rejections.size() == 4);
    for (std::uint32_t a = 0; a < 4; ++a) {
      const wg::core::RejectionRecord& r = prov.rejections[a];
      CHECK(r.attempt == a);
      CHECK(r.stage == "c03_fit");
      CHECK(r.check_id == "RAIN_RANGES[0]");
      CHECK(r.measured == 0.0);
      CHECK(r.required == 1.0);
    }
    // c01, c02 and the rejecting c03 in every attempt.
    REQUIRE(failure.report.stages.size() == 12);
    CHECK(failure.report.stages.back().kind == StageResult::Kind::reject);
  }

  SUBCASE("the final verification no attempt can pass") {
    GenParams p = small_params();
    p.checks.subregion_max = 0;
    p.checks.max_attempts = 2;
    auto run = pipeline->run(p, kSeed, wg::pipeline::RunContext{.exec = exec}, inputs);
    REQUIRE_FALSE(run);
    const wg::pipeline::FailureReport& failure = run.error();
    CHECK(failure.status.code == StatusCode::failed_precondition);
    REQUIRE(failure.report.provenance.rejections.size() == 2);
    CHECK(failure.report.provenance.rejections[1].stage == "c11_verify");
    CHECK(failure.report.provenance.rejections[1].check_id == "SUBREGION_MAX");
    // Every stage ran in both attempts, so every stage has its metrics in the report.
    REQUIRE(failure.report.stages.size() == 2 * kClassic.size());
    for (std::size_t i = 0; i < failure.report.stages.size(); ++i) {
      CHECK(failure.report.stages[i].stage == kClassic[i % kClassic.size()]);
    }
  }
}

TEST_CASE("checks.measured attempts are consistent with the product of the pass rates") {
  wg::pipeline::StageRegistry registry;
  REQUIRE(registry.add("x01_coin", [] { return std::make_unique<CoinStage>("x01_coin", kPass1); })
              .ok());
  REQUIRE(registry.add("x02_coin", [] { return std::make_unique<CoinStage>("x02_coin", kPass2); })
              .ok());
  const wg::pipeline::Profile profile{.name = "coins", .stages = {"x01_coin", "x02_coin"}};
  auto pipeline = wg::pipeline::Pipeline::create(profile, registry);
  REQUIRE(pipeline);

  GenParams p;
  p.grid.size = 9;
  p.checks.max_attempts = 1000;
  wg::core::SerialExecutor exec;
  constexpr std::size_t kRuns = 400;
  wg::pipeline::AttemptStatistics stats;
  for (std::uint64_t seed = 0; seed < kRuns; ++seed) {
    auto run = pipeline->run(p, seed, wg::pipeline::RunContext{.exec = exec});
    REQUIRE(run);
    REQUIRE(wg::pipeline::add_report(stats, run->report).ok());
  }
  REQUIRE(stats.reports == static_cast<std::int64_t>(kRuns));
  REQUIRE(stats.stages.size() == 2);

  // Attempts are geometric with q = p1 p2: mean 1/q, variance (1 - q) / q^2.
  const double q = kPass1 * kPass2;
  const double sd_mean = std::sqrt((1.0 - q) / (q * q) / static_cast<double>(kRuns));
  CHECK(std::abs(stats.mean_attempts() - (1.0 / q)) < 2.5758293035489004 * sd_mean);
  // Every run ends with one success, so the estimated product equals the measured mean.
  CHECK(stats.expected_attempts() == doctest::Approx(stats.mean_attempts()));

  // Each stage's rejections are binomial in the attempts that reached it.
  const std::array<double, 2> pass = {kPass1, kPass2};
  for (std::size_t i = 0; i < 2; ++i) {
    const wg::pipeline::StageAttempts& s = stats.stages[i];
    const wg::test::CountInterval ci =
        wg::test::binomial_interval99(static_cast<std::size_t>(s.runs), 1.0 - pass[i]);
    CHECK(static_cast<double>(s.rejections) >= ci.lo);
    CHECK(static_cast<double>(s.rejections) <= ci.hi);
  }
}
