# Performance measurements

R6 (plan section 2) proposes these budgets: classic profile at N = 257 in at most 10 s per
attempt on an 8-core desktop, physical profile at N = 1025 in at most 120 s per attempt. Shared
CI runners vary too much for absolute budgets (plan section 6.8). The `bench` job on `main`
therefore only compares each run with the previous one (ADR 0021), and the absolute numbers are
measured by hand on the reference machine and recorded here.

## Classic, N = 257 (A23, 2026-10-25)

Machine: AMD Ryzen 7 3800X (8 cores, 16 threads), Windows 11, MSVC 19.29 (Visual Studio 2019)
`/O2 /fp:precise`, `--threads 8`. Schema defaults, profile `classic`.

**100-seed sweep** (seeds 1–100):

```text
wgcli sweep --profile classic --size 257 --seeds 1..100 --threads 8 --metrics sweep.csv --keep-failures failures
python tools/ci/check_sweep.py sweep.csv --failures failures --budget-ms 10000
```

| | Before A23 | With the failed-basin memo (ADR 0021) |
|---|---|---|
| Seeds ok / failed | 100 / 0 | 100 / 0 |
| Attempts per seed | 1 | 1 |
| Median per attempt | 3.7 s | 3.1 s |
| p90 / p95 | 11.8 s / 25.6 s | 4.9 s / 5.4 s |
| Slowest seed | 37.3 s (seed 93) | 9.0 s (seed 90) |
| Seeds over 10 s | 13 | 0 |
| Stage time of the whole sweep | 10 min 16 s | 5 min 56 s |

In the final sweep seed 81 took 10.9 s once. On its own it takes 4.2 s (median of 3), so that
was a load spike from another process on the machine, not the generator.

**Per stage** (`wgcli bench --repeat 3`, medians in ms):

| Stage | Seed 1 (typical) | Seed 62 | Seed 90 (slowest) |
|---|---:|---:|---:|
| c01_allocate | 0.1 | 0.1 | 0.1 |
| c02_seed | 5.2 | 5.6 | 5.5 |
| c03_fit | 6.6 | 6.5 | 6.5 |
| c04_vegetation | 3.2 | 3.3 | 3.2 |
| c05_shape | 4.3 | 4.6 | 4.7 |
| c06_rivers | 3150 | 7776 | 9025 |
| c07_smooth | 1.8 | 1.7 | 1.4 |
| c08_climate | 1.5 | 1.5 | 1.4 |
| c09_biomes | 2.6 | 2.8 | 2.7 |
| c10_subsurface | 7.6 | 8.0 | 7.6 |
| c11_verify | 0.2 | 0.2 | 0.2 |

The live field storage stays below 4.2 MiB.

c06_rivers takes more than 99 % of the time, and nearly all of that is the test-river walkers
of `hydro::erode`. For seed 90: 555,000 walkers over 250 cycles take 68 million steps in about
8 s, and thermal erosion takes 0.7 s. About 47 million of those steps are flood cells of floods
that do find their spill. Each walker floods the same depression again and lowers its saddle by
`dig_depth` (R6.3, ADR 0014). Making this faster without changing the output is left to A30.
