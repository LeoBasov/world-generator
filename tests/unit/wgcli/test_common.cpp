#include "common.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <sstream>
#include <vector>

#include "support/temp_dir.hpp"

using wg::cli::ExitCode;
using wg::core::StatusCode;

TEST_CASE("wgcli.common maps statuses to exit codes") {
  const auto code = [](StatusCode c) {
    return wg::cli::exit_code_for(wg::core::make_status(c, "x"));
  };
  CHECK(code(StatusCode::ok) == ExitCode::ok);
  CHECK(code(StatusCode::invalid_argument) == ExitCode::invalid_input);
  CHECK(code(StatusCode::not_found) == ExitCode::invalid_input);
  CHECK(code(StatusCode::parse_error) == ExitCode::invalid_input);
  CHECK(code(StatusCode::unsupported_version) == ExitCode::invalid_input);
  CHECK(code(StatusCode::io_error) == ExitCode::invalid_input);
  CHECK(code(StatusCode::failed_precondition) == ExitCode::invalid_input);
  CHECK(code(StatusCode::internal) == ExitCode::internal);
  CHECK(code(StatusCode::out_of_memory) == ExitCode::internal);
  CHECK(code(StatusCode::cancelled) == ExitCode::internal);

  std::ostringstream out;
  std::ostringstream err;
  const wg::cli::Io io{out, err};
  CHECK(wg::cli::fail(io, wg::core::make_status(StatusCode::not_found, "gone")) == 2);
  CHECK(err.str() == "wgcli: gone\n");
}

TEST_CASE("wgcli.common parses seed lists") {
  using V = std::vector<std::uint64_t>;
  CHECK(wg::cli::parse_seed_list("7") == V{7});
  CHECK(wg::cli::parse_seed_list("1..4") == V{1, 2, 3, 4});
  CHECK(wg::cli::parse_seed_list("1,5,10..12") == V{1, 5, 10, 11, 12});
  CHECK(wg::cli::parse_seed_list("18446744073709551614..18446744073709551615") ==
        V{18446744073709551614ULL, 18446744073709551615ULL});
  for (const char* bad : {"", "x", "1,", "3..1", "1..", "..2", "0..10000000"}) {
    CAPTURE(bad);
    CHECK_FALSE(wg::cli::parse_seed_list(bad));
  }
}

TEST_CASE("wgcli.common resolves names and paths") {
  wg::test::TempDir dir("wgcli_common");
  const auto file = dir / "profiles" / "x.json";
  REQUIRE(wg::cli::write_file(file, "{}").ok());
  auto by_name = wg::cli::resolve_named_file("profile", "x", dir / "profiles");
  REQUIRE(by_name);
  CHECK(*by_name == file);
  auto by_path = wg::cli::resolve_named_file("profile", file.string(), dir.path());
  REQUIRE(by_path);
  CHECK(*by_path == file);
  auto missing = wg::cli::resolve_named_file("profile", "y", dir / "profiles");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == StatusCode::not_found);

  CHECK(wg::cli::read_file(file) == "{}");
  CHECK(wg::cli::read_file(dir / "nope").error().code == StatusCode::io_error);
}

TEST_CASE("wgcli.common loads run parameters") {
  auto defaults = wg::cli::load_run_params({}, {}, wg::cli::default_data_dir());
  REQUIRE(defaults);
  CHECK(defaults->params == wg::params::GenParams{});

  auto preset = wg::cli::load_run_params("small_island", "33", wg::cli::default_data_dir());
  REQUIRE(preset);
  CHECK(preset->params.grid.size == 33);
  CHECK(preset->profile == "classic");

  CHECK(wg::cli::load_run_params({}, "30", wg::cli::default_data_dir()).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::cli::load_run_params({}, "big", wg::cli::default_data_dir()).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::cli::load_run_params("no_such_preset", {}, wg::cli::default_data_dir()).error().code ==
        StatusCode::not_found);
}

TEST_CASE("wgcli.common builds executors and the registry") {
  auto serial = wg::cli::make_executor(1);
  REQUIRE(serial);
  CHECK((*serial)->concurrency() == 1);
  auto pool = wg::cli::make_executor(4);
  REQUIRE(pool);
  CHECK((*pool)->concurrency() == 4);
  CHECK_FALSE(wg::cli::make_executor(-1));
  CHECK_FALSE(wg::cli::make_executor(100000));

  auto registry = wg::cli::make_registry();
  REQUIRE(registry);
  CHECK(registry->contains("m01_seed"));
  CHECK(registry->contains("m03_verify"));
}

TEST_CASE("wgcli.common log sink filters by level") {
  std::ostringstream text;
  wg::cli::StreamLogSink sink(text, wg::core::LogLevel::warning);
  CHECK_FALSE(sink.enabled(wg::core::LogLevel::info));
  sink.write(wg::core::LogLevel::info, "hidden");
  sink.write(wg::core::LogLevel::warning, "shown");
  CHECK(text.str() == "wgcli: warning: shown\n");
}
