# Changelog

All notable changes to worldgen-core are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Repository layout of the project plan (section 8).
- CMake build with presets (debug, release, relwithdebinfo, asan, tsan, coverage) and
  an installable `worldgen_core` CMake package.
- `wg::core::version()` and a placeholder `wgcli` that prints the library version.
- doctest runner `wg_tests` with CTest registration of every test case.
- nanobench benchmark runner `wg_bench` and example program `wg_example`; options
  `WG_BUILD_BENCH`, `WG_BUILD_EXAMPLES` and `WG_WITH_EXR`.
- GitHub Actions, push-triggered only (plan section 6.8):
  - `ci.yml` on every push: clang-format, build and test on Linux (GCC, Clang), Windows (MSVC)
    and macOS (Apple Clang), ASan + UBSan, line coverage with an 80 % gate, and clang-tidy on the
    files changed against `main`. On pushes to `main` also TSan, every preset on the primary and
    extended runner matrix, and a benchmark run.
  - `release.yml` on `v*.*.*` tags: packages per platform, Doxygen docs on GitHub Pages and a
    GitHub Release with checksums and the CHANGELOG section.
  - `golden-update.yml` on `golden/**` branches: per-toolchain golden hash files as artifacts.
  - Compiler cache (ccache / sccache) and FetchContent cache.
- Importable ruleset for `main` (`.github/rulesets/main.json`), pull request template,
  CODEOWNERS, clang-format and clang-tidy configuration, ADR template and ADR 0001.
