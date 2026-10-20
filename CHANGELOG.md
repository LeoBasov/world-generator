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
- GitHub Actions `ci.yml`: clang-format check, build and test on Linux (GCC, Clang),
  Windows (MSVC) and macOS (Apple Clang), ASan + UBSan, and line coverage with an 80 % gate.
- Dependabot for GitHub Actions, CODEOWNERS, clang-format and clang-tidy configuration.
