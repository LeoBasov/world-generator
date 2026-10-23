# wgcli reference

`wgcli` is the command-line tool of worldgen-core (plan section 6.7). It generates, sweeps,
inspects, compares, hashes and exports worlds without Godot. It calls only the public API of the
library, so a world it generates is byte-identical to one generated through the Godot binding
with the same parameters, profile and seed.

The help blocks below are copied from the tool itself. The unit test `wgcli.docs` fails when
they differ from the built tool's `--help` output. To update them after changing a command,
build wgcli and run:

```bash
python tools/wgcli/sync_cli_doc.py --wgcli build/debug/tools/wgcli/wgcli
```

## Overview

<!-- BEGIN wgcli help -->
```text
Usage: wgcli <command> [options]
       wgcli help [<command>]
       wgcli --version

Command-line tool of worldgen-core: generate, sweep, inspect, compare, hash and
export worlds.

Commands:
  generate  Run a profile and write a world
  sweep     Run many seeds and tabulate the outcomes
  inspect   Print header, provenance and field statistics of a world
  compare   Field-wise difference of two worlds
  hash      World, field and stage hashes for golden tests
  export    Export fields as PNG16 or EXR, vector layers as JSON
  validate  Check parameter, profile and data files
  bench     Time every stage over repeated runs

Exit codes:
  0  success
  1  generation failed after all attempts (compare: the worlds differ)
  2  invalid input: arguments, missing or invalid files
  3  internal error

Run 'wgcli <command> --help' for the options of a command.
```
<!-- END -->

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success. |
| 1 | Generation failed after all attempts (every attempt was rejected by a check). `compare` also uses 1 when the worlds differ. |
| 2 | Invalid input: an unknown command or option, a missing argument, a file that is missing, malformed, of a newer version or invalid (with the JSON path of each problem), a profile whose stages are unknown or not causally ordered. |
| 3 | Internal error: a bug, or memory ran out. |

Diagnostics go to stderr, results to stdout.

### Profiles, parameters and the data directory

- `--profile` takes a profile file, or a name looked up as `DIR/profiles/<name>.json`. Without
  it, the `profile` named in the params file is used.
- `--params` takes a params file, or a preset name looked up as `DIR/presets/<name>.json`.
  Without it, the schema defaults are used. A params file lists only what differs from the
  defaults. Its `seed` is the default for `--seed`.
- `--size` overrides `grid.size` (N = 2^k + 1).
- `DIR` is `--data-dir`, by default the `data/` directory of the source tree the tool was built
  from. An installed tool finds the data under `share/worldgen_core/data` and needs
  `--data-dir` pointing there.
- The data tables of `DIR` are loaded for every generation and passed to the stages.

Until the first real stages land (A11), the only stages are the mock stages of
`tests/fixtures/profiles/mock.json`, which wgcli registers itself:

| Stage | Reads | Writes |
|---|---|---|
| `m01_seed` | — | `latitude` |
| `m02_shape` | `latitude` | `elevation`, `peaks` |
| `m03_verify` | `elevation`, `peaks` | `biome`; rejects with check `MOCK_VERIFY` for about a quarter of all attempts, decided by its RNG key |

### Hashes

`wgcli hash` and `generate` print the world hash defined in
[ADR 0008](adr/0008-world-hashes.md): the XXH3-64 of the grid size, every field's raw bytes and
the vector layers. It excludes metrics, wall times, the provenance and the library version, so a
generated world and its file hash the same. Hashes print as `0x` and 16 hex digits.

### Golden seed list

`wgcli hash --seeds FILE` hashes every case and seed of a seed list, the input of the
`golden-update.yml` workflow (`tests/golden/seeds.json`, schema
`schema/golden_seeds.schema.json`):

```json
{
  "$schema": "../../schema/golden_seeds.schema.json",
  "schema_version": 1,
  "cases": [
    { "name": "classic_small", "profile": "classic", "params": "small_island", "size": 65,
      "seeds": [1, 2, 3] }
  ]
}
```

`profile` and `params` are paths relative to the seed list, or names looked up in the data
directory. `params` and `size` are optional. With `--out`, the result is JSON:
`{"schema_version": 1, "cases": [{"name": ..., "results": [{"seed": 1, "attempt": 0,
"status": "ok", "world": "0x...", "stages": [...]}]}]}`. A seed whose attempts all fail has
`"status": "failed"` and no hash.

## Commands

### generate

<!-- BEGIN wgcli generate --help -->
```text
Usage: wgcli generate [options]

Run a profile and write a world.

Runs the stages of the profile with the parameters and seed and writes the world
file. A rejected attempt restarts with a new stage key (up to checks.max_attempts).
Snapshots are world files named a<attempt>_<index>_<stage>.wgw: 'final' keeps the
finished world, 'all' every completed stage of every attempt.

Options:
  --profile NAME|FILE         Profile name in DIR/profiles, or a profile file
  --params NAME|FILE          Preset name in DIR/presets, or a params file
  --seed N                    World seed (default: the params file's, else 0)
  --size N                    Grid size N = 2^k + 1, overrides grid.size
  --threads T                 Worker threads, 0 = one per core (default 1)
  --snapshots none|final|all  Also write snapshots to OUT.snapshots/ (default none)
  --out FILE                  World file to write (.wgw), required
  --data-dir DIR              Data directory (default: the built-in one)
  --quiet                     Print warnings only on failure
  -h, --help                  Show this help and exit
```
<!-- END -->

Example:

```bash
wgcli generate --profile tests/fixtures/profiles/mock.json --size 65 --seed 7 --snapshots all --out world.wgw
```

### sweep

<!-- BEGIN wgcli sweep --help -->
```text
Usage: wgcli sweep [options]

Run many seeds and tabulate the outcomes.

Generates one world per seed without writing it and records the status, attempts,
rejections and stage metrics of each. Failed seeds do not change the exit code: a
sweep measures failure rates.

Options:
  --seeds LIST         Seeds such as 1..1000 or 1,5,9, required
  --metrics FILE       Write one CSV row per seed
  --keep-failures DIR  Write seed_<s>.json for every failed seed
  --profile NAME|FILE  Profile name in DIR/profiles, or a profile file
  --params NAME|FILE   Preset name in DIR/presets, or a params file
  --size N             Grid size N = 2^k + 1, overrides grid.size
  --threads T          Worker threads, 0 = one per core (default 1)
  --data-dir DIR       Data directory (default: the built-in one)
  --quiet              Print warnings only on failure
  -h, --help           Show this help and exit
```
<!-- END -->

The metrics CSV has the columns `seed,status,attempts,rejections,last_check,wall_ms`, followed by
one `<stage>.<metric>` column per metric a stage reported in the successful attempt, sorted by
name. `status` is `ok` or `failed`; `wall_ms` sums every stage run of every attempt. A failure
report (`--keep-failures`) holds the status, the rejections and the result of every stage run.

### inspect

<!-- BEGIN wgcli inspect --help -->
```text
Usage: wgcli inspect WORLD [options]

Print header, provenance and field statistics of a world.

Reads the header, chunk table and metadata of WORLD, then only the chunks of the
requested fields, and prints min, max, mean and the count of non-finite values.

Options:
  --field NAME ...  Field to report (default: every field)
  --histogram BINS  Also print a histogram with BINS bins per field
  -h, --help        Show this help and exit
```
<!-- END -->

### compare

<!-- BEGIN wgcli compare --help -->
```text
Usage: wgcli compare A B [options]

Field-wise difference of two worlds.

Compares the fields of two worlds on the same grid and their vector layers. Prints
the largest absolute difference and the number of cells above the tolerance per
field. Exits with 0 if the worlds agree within the tolerance, 1 if they differ.

Options:
  --field NAME ...  Field to compare (default: every field)
  --tolerance T     Largest accepted absolute difference (default 0)
  --diff-png DIR    Write DIR/<field>_diff.png of |A - B| per differing field
  -h, --help        Show this help and exit
```
<!-- END -->

Two NaN values count as equal. A field present in only one world counts as a difference.

### hash

<!-- BEGIN wgcli hash --help -->
```text
Usage: wgcli hash [WORLD] [options]

World, field and stage hashes for golden tests.

Hashes a world file, or generates a world (--profile ...) and hashes it, or hashes
every case and seed of a golden seed list (--seeds). The world hash covers the grid
size, every field's raw bytes and the vector layers, never metrics or versions
(ADR 0008), so it is the same for a generated world and its file.

Options:
  --per-stage          Also hash the state after every stage (needs a generation)
  --fields             Also print the hash of every field
  --seeds FILE         Golden seed list: hash every case and seed of FILE
  --out FILE           Write the hashes as JSON to FILE
  --profile NAME|FILE  Profile name in DIR/profiles, or a profile file
  --params NAME|FILE   Preset name in DIR/presets, or a params file
  --seed N             World seed (default: the params file's, else 0)
  --size N             Grid size N = 2^k + 1, overrides grid.size
  --threads T          Worker threads, 0 = one per core (default 1)
  --data-dir DIR       Data directory (default: the built-in one)
  --quiet              Print warnings only on failure
  -h, --help           Show this help and exit
```
<!-- END -->

### export

<!-- BEGIN wgcli export --help -->
```text
Usage: wgcli export WORLD [options]

Export fields as PNG16 or EXR, vector layers as JSON.

png16 writes DIR/<field>.png scaled by the field's registry range (or its data range)
and stores the range in the PNG. exr writes DIR/<field>.exr with the values unscaled
(only if the library was built with WG_WITH_EXR). json writes DIR/layers.json.
Colour maps are for looking at, never for reading back.

Options:
  --format png16|exr|json          Output format, required
  --field NAME|all ...             Field to export (default all; not for json)
  --colormap grey|terrain|viridis  Also write <field>_<map>.png for review (png16 only)
  --out DIR                        Output directory, required
  -h, --help                       Show this help and exit
```
<!-- END -->

### validate

<!-- BEGIN wgcli validate --help -->
```text
Usage: wgcli validate [options]

Check parameter, profile and data files.

Checks every rule the loaders apply: types, bounds, unknown keys and cross-field
rules of parameters; stage ids and causal order of profiles; the data tables and the
rules across them. Without options it checks the built-in data directory. Prints one
line per issue with its JSON path; warnings alone do not fail.

Options:
  --params FILE ...   Parameter file to check
  --profile FILE ...  Profile file to check against the stage registry
  --data-dir DIR      Check the tables, presets and profiles of DIR
  -h, --help          Show this help and exit
```
<!-- END -->

### bench

<!-- BEGIN wgcli bench --help -->
```text
Usage: wgcli bench [options]

Time every stage over repeated runs.

Generates the same world R times and prints min, median and max wall time and the
memory owned after each stage of the successful attempt. Times vary between runs
and machines; they never enter a hash.

Options:
  --repeat R           Runs per stage (default 3)
  --json FILE          Also write the table as JSON
  --profile NAME|FILE  Profile name in DIR/profiles, or a profile file
  --params NAME|FILE   Preset name in DIR/presets, or a params file
  --seed N             World seed (default: the params file's, else 0)
  --size N             Grid size N = 2^k + 1, overrides grid.size
  --threads T          Worker threads, 0 = one per core (default 1)
  --data-dir DIR       Data directory (default: the built-in one)
  --quiet              Print warnings only on failure
  -h, --help           Show this help and exit
```
<!-- END -->
