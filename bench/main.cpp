// wg_bench: nanobench benchmarks of worldgen-core.
// Kernel and stage benchmarks join as their work packages land; `--json <file>` writes the
// results for the CI benchmark job (plan section 6.8).

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

// Executor (A05): float64 sum of 2^20 floats with a fixed grain, the shape of a field statistic.
const std::vector<float> kBenchValues(std::size_t{1} << 20U, 1.0F);

void bench_reduce_sum(wg::core::Executor& exec) {
  const auto map = [](const wg::core::ChunkRange& r) {
    double s = 0.0;
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      s += kBenchValues[static_cast<std::size_t>(i)];
    }
    return s;
  };
  const auto add = [](double a, double b) { return a + b; };
  const auto n = static_cast<std::int64_t>(kBenchValues.size());
  const auto sum = wg::core::parallel_reduce(exec, 0, n, 16384, 0.0, map, add);
  ankerl::nanobench::doNotOptimizeAway(sum.value_or(0.0));
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

  wg::core::SerialExecutor serial;
  bench.run("core.exec.reduce_sum(serial)", [&] { bench_reduce_sum(serial); });
  auto pool = wg::core::ThreadPool::create(8);
  if (pool) {
    wg::core::ThreadPool& p = **pool;
    bench.run("core.exec.reduce_sum(8 threads)", [&] { bench_reduce_sum(p); });
  }

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
