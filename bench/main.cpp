// wg_bench: nanobench benchmarks of worldgen-core.
// Kernel and stage benchmarks join as their work packages land; `--json <file>` writes the
// results for the CI benchmark job (plan section 6.8).

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <nanobench.h>

#include "wg/wg.hpp"

namespace {

void bench_version() { ankerl::nanobench::doNotOptimizeAway(wg::core::version().major); }

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
