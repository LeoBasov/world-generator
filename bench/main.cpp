// wg_bench: nanobench benchmarks of worldgen-core.
// Kernel and stage benchmarks join as their work packages land; `--json <file>` writes the
// results for the CI benchmark job (plan section 6.8), `--quick` leaves out the classic profile
// at N = 257 (the smoke run of every push).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Numerics (A06) on a 257 x 257 grid, the classic-profile size.
struct NumericsInputs {
  wg::core::Grid grid{.n = 257, .dx_m = 100.0, .hu_m = 1.0};
  std::vector<float> z;
  std::vector<std::uint8_t> mask;   // half of the cells
  std::vector<std::uint8_t> seeds;  // one cell in a thousand
  std::vector<float> out;
  std::vector<double> out64;
  std::vector<std::int32_t> labels;
};

NumericsInputs make_numerics_inputs() {
  NumericsInputs in;
  const auto cells = static_cast<std::size_t>(in.grid.cells());
  const wg::core::RngKey key = wg::core::stage_key(1, "bench.numerics", 0);
  in.z.resize(cells);
  in.mask.resize(cells);
  in.seeds.resize(cells);
  for (std::size_t i = 0; i < cells; ++i) {
    in.z[i] = static_cast<float>(wg::core::uniform01(key, i, 0));
    in.mask[i] = static_cast<std::uint8_t>(wg::core::uniform01(key, i, 1) < 0.5 ? 1 : 0);
    in.seeds[i] = static_cast<std::uint8_t>(wg::core::uniform01(key, i, 2) < 0.001 ? 1 : 0);
  }
  in.out.resize(cells);
  in.out64.resize(cells);
  in.labels.resize(cells);
  return in;
}

void bench_numerics(ankerl::nanobench::Bench& bench, wg::core::Executor& exec,
                    const std::string& suffix, NumericsInputs& in) {
  bench.run("num.laplacian" + suffix, [&] {
    ankerl::nanobench::doNotOptimizeAway(wg::num::laplacian(in.z, {}, in.grid, in.out, exec).ok());
  });
  bench.run("num.d8_slope" + suffix, [&] {
    ankerl::nanobench::doNotOptimizeAway(wg::num::d8_slope(in.z, in.grid, in.out, {}, exec).ok());
  });
  bench.run("num.edt_squared" + suffix, [&] {
    ankerl::nanobench::doNotOptimizeAway(
        wg::num::edt_squared(in.seeds, in.grid, in.out64, exec).ok());
  });
}

void bench_numerics_serial_only(ankerl::nanobench::Bench& bench, NumericsInputs& in) {
  bench.run("num.label_components(d8)", [&] {
    ankerl::nanobench::doNotOptimizeAway(
        wg::num::label_components(in.mask, in.grid, wg::num::Connectivity::d8, in.labels)
            .value_or(0));
  });
  bench.run("num.heap(push+pop all cells)", [&] {
    auto heap = wg::num::IndexHeap<float>::create(in.z.size());
    if (!heap) {
      return;
    }
    for (std::size_t i = 0; i < in.z.size(); ++i) {
      static_cast<void>(heap->push(in.z[i], static_cast<std::int64_t>(i)));
    }
    while (!heap->empty()) {
      ankerl::nanobench::doNotOptimizeAway(heap->pop().cell);
    }
  });
  bench.run("num.radial_power_spectrum", [&] {
    ankerl::nanobench::doNotOptimizeAway(
        wg::num::radial_power_spectrum(in.z, in.grid, {}).has_value());
  });
}

// Fractal field (A11): one primary field with the default elevation parameters, at the classic
// size and at a physical-profile simulation size.
void bench_fractal(ankerl::nanobench::Bench& bench, wg::core::Executor& exec,
                   const std::string& suffix) {
  const wg::params::FieldGenParams params = wg::params::FieldsParams{}.elevation;
  const wg::core::RngKey key = wg::core::stage_key(1, "bench.fractal", 0);
  for (const std::int32_t n : {257, 1025}) {
    const wg::core::Grid grid{.n = n, .dx_m = 100.0, .hu_m = 1.0};
    std::vector<float> z(static_cast<std::size_t>(grid.cells()));
    bench.run("fractal.generate_field(" + std::to_string(n) + ")" + suffix, [&] {
      ankerl::nanobench::doNotOptimizeAway(
          wg::fractal::generate_field(grid, params, key, {}, z, exec).ok());
    });
  }
}

// World file (A09): write and read a 257 x 257 world with four fields (two smooth, one noise,
// one categorical), with and without zstd.
std::optional<wg::core::WorldState> make_bench_world(const NumericsInputs& in,
                                                     const wg::params::GenParams& p) {
  auto created = wg::core::WorldState::create(in.grid);
  if (!created) {
    return std::nullopt;
  }
  wg::core::WorldState state = std::move(*created);
  const auto fill = [&](wg::core::FieldId id, auto value) {
    auto view = state.write<float>(id);
    for (std::size_t i = 0; view && i < view->data.size(); ++i) {
      view->data[i] = value(i);
    }
  };
  const auto n = static_cast<std::size_t>(in.grid.n);
  fill(wg::core::FieldId::elevation, [&](std::size_t i) { return in.z[i] * 400.0F; });
  fill(wg::core::FieldId::latitude, [&](std::size_t i) {
    const std::size_t row = i / n;
    return static_cast<float>(row) * 0.01F;
  });
  fill(wg::core::FieldId::rainfall,
       [&](std::size_t i) { return static_cast<float>(i % n) * 0.4F; });
  if (auto water = state.write<std::uint8_t>(wg::core::FieldId::water)) {
    for (std::size_t i = 0; i < water->data.size(); ++i) {
      water->data[i] = in.mask[i];
    }
  }
  state.provenance().params_hash = wg::params::params_hash(p);
  return state;
}

void bench_world_file(ankerl::nanobench::Bench& bench, const NumericsInputs& in) {
  wg::params::GenParams params;
  params.grid.size = in.grid.n;
  params.grid.dx_m = in.grid.dx_m;
  params.grid.hu_m = in.grid.hu_m;
  const std::optional<wg::core::WorldState> world = make_bench_world(in, params);
  if (!world) {
    return;
  }
  const wg::core::WorldState& state = *world;
  for (const wg::io::Codec codec : {wg::io::Codec::zstd, wg::io::Codec::raw}) {
    const std::string name = codec == wg::io::Codec::zstd ? "zstd" : "raw";
    const wg::io::WriteOptions options{.codec = codec};
    bench.run("io.write_world(" + name + ")", [&] {
      ankerl::nanobench::doNotOptimizeAway(wg::io::write_world(state, params, options).has_value());
    });
    const auto file = wg::io::write_world(state, params, options);
    if (!file) {
      continue;
    }
    bench.run("io.read_world(" + name + ")", [&] {
      ankerl::nanobench::doNotOptimizeAway(wg::io::read_world_buffer(*file).has_value());
    });
  }
}

// Classic profile (A23): one whole run of data/profiles/classic.json with the schema defaults
// and seed 1, a typical seed (ADR 0021), five times each. N = 257 is the size of the R6 budget.
void bench_classic(ankerl::nanobench::Bench& bench, wg::core::Executor& exec, bool quick) {
  const std::filesystem::path data{WG_BENCH_DATA_DIR};
  auto tables = wg::params::load_tables_dir(data);
  auto profile = wg::pipeline::load_profile_file(data / "profiles" / "classic.json");
  wg::pipeline::StageRegistry registry;
  if (!tables || !profile || !wg::stages::add_classic_stages(registry).ok()) {
    std::cerr << "wg_bench: cannot load the classic profile or the data tables\n";
    return;
  }
  auto pipeline = wg::pipeline::Pipeline::create(*profile, registry);
  if (!pipeline) {
    std::cerr << "wg_bench: " << pipeline.error().message << "\n";
    return;
  }
  bench.epochs(5).epochIterations(1).warmup(0);
  for (const std::int32_t n : {65, 257}) {
    if (quick && n > 65) {
      break;
    }
    wg::params::GenParams params;
    params.grid.size = n;
    bench.run("classic.pipeline(" + std::to_string(n) + ")", [&] {
      ankerl::nanobench::doNotOptimizeAway(
          pipeline
              ->run(params, 1, wg::pipeline::RunContext{.exec = exec},
                    wg::pipeline::RunInputs{.tables = &*tables, .initial = nullptr})
              .has_value());
    });
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string json_path;
  bool quick = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--json" && i + 1 < argc) {
      json_path = argv[++i];
    } else if (arg == "--quick") {
      quick = true;
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

  NumericsInputs numerics = make_numerics_inputs();
  bench_numerics(bench, serial, "(serial)", numerics);
  if (pool) {
    bench_numerics(bench, **pool, "(8 threads)", numerics);
  }
  bench_numerics_serial_only(bench, numerics);
  bench_fractal(bench, serial, "(serial)");
  if (pool) {
    bench_fractal(bench, **pool, "(8 threads)");
  }
  bench_world_file(bench, numerics);
  if (pool) {
    bench_classic(bench, **pool, quick);
  } else {
    bench_classic(bench, serial, quick);
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
