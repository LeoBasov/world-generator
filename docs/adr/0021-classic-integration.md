# ADR 0021: Classic integration: profile, golden hashes, failed-basin memo and baselines

- Status: accepted
- Date: 2026-10-25
- Work package: A23

## Context

A12–A22 delivered the eleven classic stages; until now they ran end to end only through test
fixture profiles. A23 (plan section 9.2) ships the classic profile and fixes what it produces:

- 100 seeds at N = 257 succeed or fail with a full report;
- hashes are identical for 1 and 8 threads (R1);
- per-platform goldens are stored (plan sections 6.8 and 10.3);
- the R6 budget is met: classic, N = 257, at most 10 s per attempt on an 8-core desktop;
- the review checklist of section 10.3 passes.

The first 100-seed sweep at N = 257 (schema defaults, MSVC 19.29 /O2, 16 hardware threads)
missed R6: median 3.7 s, but 13 seeds above 10 s and a maximum of 37 s. Stage `c06_rivers`
took more than 99 % of the time, every other stage less than 10 ms. The slow seeds made up to
ten times as many walker steps as the others (seed 2: 229 million against about 24 million).
Their test rivers run into a large closed basin. Each walker floods it in (z, index) order and
spends its whole step limit (1024) before it reaches a cell below the water level, so it digs
nothing. Every later walker of the cycle, and of every one of the 250 cycles, repeats the same
futile flood.

## Decision

**Profile.** `data/profiles/classic.json` lists c01_allocate … c11_verify. The presets already
name it (`"profile": "classic"`). The fixture `tests/fixtures/profiles/classic_verify.json` is
removed. The prefix fixtures (seed, rivers, biomes, geology) stay for the stage tests.

**Failed-basin memo (amends ADR 0014).** `hydro::erode` keeps one byte per cell, `failed`,
cleared at the start of every cycle and passed to every walker of the cycle (`run_walker`'s new
trailing argument).
- A flood that ends without an exit, at the step limit or stuck, marks every cell it took,
  the pit included.
- A walker that stands on a marked cell, or whose flood takes one, stops with the new
  `WalkerStop::failed_basin`. It digs nothing, which is what the repeated flood would have
  done.

So each cycle floods a basin that cannot be drained within the step limit once, not once per
walker. The memo is a heuristic: a later walker that enters the basin with more of its budget
left, or from another side, might have reached the spill. It changes `c06_rivers` output, and
it is deterministic because the walkers of a cycle run serially in source order. Before goldens
existed this was the time to change it.

Two output-preserving changes speed up the same loops by about 15 %:
- the walker's flood heap holds 12-byte entries ordered by a function object;
- the walker and `thermal_erode` take a cell's 8 neighbours from the internal
  `num::detail::neighbors8`, which divides once per cell instead of twice per direction, and the
  thermal FIFO wraps without a modulo.

Effect on the reference machine (`docs/performance.md`):
- 100 of 100 seeds at N = 257 succeed in one attempt;
- the median falls from 3.7 s to 3.1 s and p95 from 25.6 s to 5.4 s;
- the slowest seed falls from 37.3 s to 9.0 s (seed 90; median of 3).

R6 is met with about 10 % to spare. The rest of the walker time goes to successful floods, which
are the defined behaviour of R6.3. A30 owns any further speed-up.

**End-to-end tests (levels 3 and 4).** `tests/integration/classic/test_classic.cpp` runs
the profile file through `Pipeline` at N = 33 and 65, four seeds each, with the schema defaults.
- A finished world keeps the invariants of the review checklist that a program can judge. Every
  field value is finite and inside its registry range. Every river mouth drains into an ocean or
  lake cell of the final `water` field. Every cell has a region.
- A failure must carry a complete report.
- For three seeds at N = 65, the fields (raw bytes) and vector layers are identical with
  `SerialExecutor` and an 8-thread `ThreadPool`.

**Golden seeds.** `tests/golden/seeds.json` has two cases:
- `classic_65`: seeds 1–20 at N = 65 with the schema defaults;
- `classic_small_island`: seeds 1–5 with the `small_island` preset, whose seeds pass only after
  about 24 rejected attempts each, so the attempt loop and stage keys are pinned too.

Both cases together take a few seconds per runner.

**Golden and determinism checks (every push).** The `build + test` job of `ci.yml` does both on
each of the four primary toolchains. They are steps, not new jobs, so the required check names
of the ruleset stay the same.
- It hashes the seed list per stage with `--threads 1` and `--threads 8`, and the files must be
  identical.
- Except on `golden/**` branches, it compares the 1-thread file with
  `tests/golden/hashes/<toolchain key>.json`.
- `tools/ci/compare_hashes.py` reports the first stage whose hash differs, per case and seed.
- A toolchain without a golden file gets a warning, not a failure. The extended-matrix runners
  only run on main, and a new runner image must not turn every branch red.

**Updating goldens.** Push the branch also as `golden/<name>`. `golden-update.yml` writes every
runner's hash file as an artifact and prints it in the job summary with its SHA-256. Commit the
files to `tests/golden/hashes/` on the branch. An intended output change also needs an ADR (plan
section 10.3), like this one for the memo.

The first run (2026-10-25) gave byte-identical files on all seven runners: GCC 14 on x64 and
arm64, Clang 18, MSVC 19.44 and 19.51, and Apple Clang 17 and 21. MSVC 19.29 on the dev machine
matches them too (SHA-256 `4ad9d34c…`). The pipeline is bit-reproducible across platforms.
The goldens are still stored per toolchain key, as plan section 6.8 requires, so a future
platform difference shows up as a failure on that toolchain alone.

The linux-clang runner of that workflow also exports the review image set of the branch (after)
and of main (before) with `tools/review/review_set.py`. That is six seeds at N = 257: elevation,
water, rainfall, temperature, drainage, vegetation, salinity, biome and region id, with an
`index.html`.

**100-seed acceptance.** `tools/ci/check_sweep.py` checks a `wgcli sweep --metrics` CSV and its
`--keep-failures` directory. Every seed must be `ok`, or have a report with status
`failed_precondition` whose rejections all name stage, check and finite measured and required
values. It prints attempts and wall time per attempt as percentiles. `--budget-ms` turns R6 into
a failure for runs on the reference machine. The main-only `ci.yml` job `classic sweep
(linux-clang)` runs seeds 1–100 at N = 257 with it. Times on shared runners are informative only.

**Benchmark baseline.** `wg_bench` gains `classic.pipeline(65)` and `classic.pipeline(257)`:
seed 1, a typical seed, five epochs of one run each. `--quick`, used by the every-push smoke run,
leaves out N = 257.

The main-only `bench` job compares the medians with the previous main run of the same runner
label (`tools/ci/bench_compare.py`). The state is kept in the Actions cache under a key per run,
restored by prefix.
- A benchmark more than 10 % slower than its baseline gets a strike.
- A strike on two consecutive pushes fails the job (plan section 6.8). The slower value then
  becomes the baseline, so the regression is reported, and must be fixed or accepted, rather
  than failing main forever.
- Otherwise the current median becomes the baseline. The first run records it.

## Consequences

- `c06_rivers` output changes for every world whose walkers hit a basin they cannot drain. Since
  no golden hashes existed before A23, no stored hash changes.
- Determinism and goldens are checked on every push. A change that alters any classic stage
  output now fails CI unless it comes with new golden files from a `golden/**` run.
- The every-push jobs take a few seconds longer (two hash runs of 25 seeds, the N = 65 pipeline
  benchmark).
- The rolling benchmark baseline lets a slow drift of less than 10 % per push through. The
  absolute R6 budget is still measured by hand on the reference machine (plan section 6.8), and
  `docs/performance.md` records that measurement.

## Alternatives considered

- **Leave R6 to A30.** Rejected: R6 is an A23 acceptance criterion, and the cause was
  algorithmic waste, not a slow kernel.
- **Keep the memo across cycles.** Rejected: digging and thermal erosion change the terrain
  between cycles, so a basin that failed in cycle c may drain in c + 1.
- **Raise or lower `walker_step_limit`.** A higher limit makes the futile floods more expensive.
  A lower one cuts legitimate long walks short.
- **Golden checks as separate jobs.** Rejected: every new job name would have to be added to
  the required checks of `.github/rulesets/main.json`, and it would rebuild wgcli.
- **A committed benchmark baseline.** Rejected: it could only come from a main run of the
  hosted runner and would need a commit to main after every intended change.
