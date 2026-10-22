#include "wg/pipeline/stage.hpp"

#include <doctest/doctest.h>

using wg::core::StatusCode;
using wg::pipeline::StageMetrics;
using wg::pipeline::StageResult;

TEST_CASE("pipeline.stage result factories") {
  const StageResult ok = StageResult::ok();
  CHECK(ok.kind == StageResult::Kind::ok);
  CHECK(ok.check_id.empty());
  CHECK(ok.status.ok());

  const StageResult rej = StageResult::reject("VOLCANO_MIN", 0.0, 3.0);
  CHECK(rej.kind == StageResult::Kind::reject);
  CHECK(rej.check_id == "VOLCANO_MIN");
  CHECK(rej.measured == 0.0);
  CHECK(rej.required == 3.0);

  const StageResult rep = StageResult::repaired("EDGE_OCEANS", 0.5, 1.0);
  CHECK(rep.kind == StageResult::Kind::repaired);
  CHECK(rep.check_id == "EDGE_OCEANS");

  CHECK(StageResult::cancelled().kind == StageResult::Kind::cancelled);

  const StageResult err =
      StageResult::error(wg::core::make_status(StatusCode::out_of_memory, "no room"));
  CHECK(err.kind == StageResult::Kind::error);
  CHECK(err.status.code == StatusCode::out_of_memory);
}

TEST_CASE("pipeline.stage metrics values") {
  StageMetrics m;
  CHECK_FALSE(m.value("iterations"));
  m.add("iterations", 250.0);
  m.add("cells", 81.0);
  m.add("iterations", 1.0);
  CHECK(m.value("iterations") == 250.0);
  CHECK(m.value("cells") == 81.0);
  CHECK(m.values.size() == 3);
}

TEST_CASE("pipeline.stage kind names") {
  CHECK(wg::pipeline::to_string(StageResult::Kind::ok) == "ok");
  CHECK(wg::pipeline::to_string(StageResult::Kind::repaired) == "repaired");
  CHECK(wg::pipeline::to_string(StageResult::Kind::reject) == "reject");
  CHECK(wg::pipeline::to_string(StageResult::Kind::cancelled) == "cancelled");
  CHECK(wg::pipeline::to_string(StageResult::Kind::error) == "error");
  CHECK(wg::pipeline::to_string(static_cast<StageResult::Kind>(99)) == "unknown");
}
