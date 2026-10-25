#include "wg/pipeline/attempts.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using wg::pipeline::AttemptStatistics;
using wg::pipeline::RunReport;
using wg::pipeline::StageRecord;
using Kind = wg::pipeline::StageResult::Kind;

namespace {

StageRecord record(const std::string& stage, std::uint32_t attempt, Kind kind) {
  return StageRecord{.stage = stage,
                     .attempt = attempt,
                     .kind = kind,
                     .check_id = kind == Kind::reject ? "CHECK" : "",
                     .measured = 0.0,
                     .required = 0.0,
                     .metrics = {}};
}

}  // namespace

TEST_CASE("pipeline.attempts counts runs and rejections per stage") {
  // Attempt 0 rejected at b, attempt 1 at c (after a repair at a), attempt 2 succeeds.
  RunReport report;
  report.stages = {record("a", 0, Kind::ok),       record("b", 0, Kind::reject),
                   record("a", 1, Kind::repaired), record("b", 1, Kind::ok),
                   record("c", 1, Kind::reject),   record("a", 2, Kind::ok),
                   record("b", 2, Kind::ok),       record("c", 2, Kind::ok)};
  AttemptStatistics stats;
  REQUIRE(wg::pipeline::add_report(stats, report).ok());
  REQUIRE(stats.stages.size() == 3);
  CHECK(stats.stages[0].stage == "a");
  CHECK(stats.stages[0].runs == 3);
  CHECK(stats.stages[0].rejections == 0);
  CHECK(stats.stages[1].runs == 3);
  CHECK(stats.stages[1].rejections == 1);
  CHECK(stats.stages[2].stage == "c");
  CHECK(stats.stages[2].runs == 2);
  CHECK(stats.stages[2].pass_rate() == doctest::Approx(0.5));
  CHECK(stats.reports == 1);
  CHECK(stats.attempts == 3);
  CHECK(stats.mean_attempts() == 3.0);
  // (1 * 2/3 * 1/2)^-1 = 3
  CHECK(stats.expected_attempts() == doctest::Approx(3.0));

  // A second report, accepted at once, lowers both.
  RunReport quick;
  quick.stages = {record("a", 0, Kind::ok), record("b", 0, Kind::ok), record("c", 0, Kind::ok)};
  REQUIRE(wg::pipeline::add_report(stats, quick).ok());
  CHECK(stats.reports == 2);
  CHECK(stats.attempts == 4);
  CHECK(stats.mean_attempts() == 2.0);
  CHECK(stats.expected_attempts() == doctest::Approx(1.0 / ((3.0 / 4.0) * (2.0 / 3.0))));
}

TEST_CASE("pipeline.attempts edge cases") {
  AttemptStatistics stats;
  CHECK(stats.mean_attempts() == 0.0);
  CHECK(stats.expected_attempts() == 1.0);
  CHECK(wg::pipeline::StageAttempts{}.pass_rate() == 1.0);

  // A stage that never passed: no finite expectation.
  RunReport failed;
  failed.stages = {record("a", 0, Kind::reject), record("a", 1, Kind::reject)};
  REQUIRE(wg::pipeline::add_report(stats, failed).ok());
  CHECK(stats.attempts == 2);
  CHECK(std::isinf(stats.expected_attempts()));

  // An empty report counts as a report without attempts.
  REQUIRE(wg::pipeline::add_report(stats, RunReport{}).ok());
  CHECK(stats.reports == 2);
  CHECK(stats.attempts == 2);
}
