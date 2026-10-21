#include "wg/core/provenance.hpp"

#include <doctest/doctest.h>

using wg::core::Provenance;

TEST_CASE("core.provenance last_completed_stage") {
  Provenance p;
  CHECK(p.last_completed_stage().empty());
  p.completed_stages = {"c01_allocate", "c02_seed"};
  CHECK(p.last_completed_stage() == "c02_seed");
}

TEST_CASE("core.provenance records rejections") {
  Provenance p;
  wg::core::RejectionRecord r;
  r.stage = "c03_fit";
  r.check_id = "PEAK_NUMBER_MIN";
  r.measured = 2.0;
  r.required = 5.0;
  p.rejections.push_back(r);
  REQUIRE(p.rejections.size() == 1);
  CHECK(p.rejections.front().check_id == "PEAK_NUMBER_MIN");
  CHECK(p.attempt == 0);
}
