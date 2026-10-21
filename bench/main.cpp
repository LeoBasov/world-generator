// wg_bench: nanobench benchmarks of worldgen-core.
// Kernel and stage benchmarks join as their work packages land; `--json <file>` writes the
// results for the CI benchmark job (plan section 6.8).

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <nanobench.h>

#include "wg/wg.hpp"

namespace {

void bench_version() { ankerl::nanobench::doNotOptimizeAway(wg::core::version().major); }

// RNG (A04): one draw per call, the cell index advancing like a per-cell kernel loop.
const wg::core::RngKey kBenchKey = wg::core::stage_key(1, "bench.rng", 0);
std::uint64_t bench_cell = 0;

void bench_uniform01() {
  ankerl::nanobench::doNotOptimizeAway(wg::core::uniform01(kBenchKey, bench_cell++, 0));
}

void bench_normal() {
  ankerl::nanobench::doNotOptimizeAway(wg::core::normal(kBenchKey, bench_cell++, 0, 0.0, 1.0));
}

void bench_poisson(double lambda) {
  wg::core::RngStream stream{kBenchKey, bench_cell++};
  ankerl::nanobench::doNotOptimizeAway(wg::core::poisson(stream, lambda));
}

}  // namespace

int main(int argc, char** argv) {
  std::string json_path;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--json" && i + 1 < argc) {
      json_path = argv[++i];
    } else {
      std::cerr << "wg_bench: unknown argument '" << arg << "'\n";
      return 2;
    }
  }

  ankerl::nanobench::Bench bench;
  bench.title("worldgen-core").warmup(100);
  bench.run("core.version", bench_version);
  bench.run("core.rng.uniform01", bench_uniform01);
  bench.run("core.rng.normal", bench_normal);
  bench.run("core.rng.poisson(4)", [] { bench_poisson(4.0); });
  bench.run("core.rng.poisson(1000)", [] { bench_poisson(1000.0); });

  if (!json_path.empty()) {
    std::ofstream out{json_path};
    if (!out) {
      std::cerr << "wg_bench: cannot write '" << json_path << "'\n";
      return 3;
    }
    ankerl::nanobench::render(ankerl::nanobench::templates::json(), bench, out);
  }
  return 0;
}
