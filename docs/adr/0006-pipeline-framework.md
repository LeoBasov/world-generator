# ADR 0006: Pipeline framework

- Status: accepted
- Date: 2026-10-22
- Work package: A08

## Context

A08 adds the module that turns stages into worlds: the stage interface, the stage registry,
profiles, the causal-order check (R2), the attempt loop with rejection records (R8), repair
hooks, the snapshot hook and per-stage metrics. Plan section 6.4 sketched the API "to be
finalised in A08". Every stage from A11 on, `wgcli run` (A10) and the Godot binding (B02)
depend on it, so its contract must be settled before the first stage lands.

## Decision

- **Stages see one `StageContext`.** The sketch's `run(WorldState&, const GenParams&, RngKey,
  RunContext&)` becomes `run(StageContext&)`. The context holds the state, the parameters, the
  data tables (C9/P10 classify with the biome table, C10/P3 read the geology tables), the key
  σ_{s,a}, the attempt, the executor, the log and progress sinks and the cancel flag. Later
  needs extend the struct, not every stage's signature.
- **Fields and layers are both contract elements.** A stage declares `inputs()` and
  `outputs()` as a `FieldSet`, a bitset over `FieldId` plus a new `LayerId` for the vector
  layers of plan 4.2 (peaks, volcanoes, river graph, lakes, regions, plates, geo columns,
  minerals). The stage tables of plan 4.3 and 4.4 list both kinds.
- **R2 is checked at `Pipeline::create`.** For every k, in(s_k) ⊆ F₀ ∪ out(s_1) ∪ … ∪
  out(s_{k−1}). F₀, the grid geometry, is not a field, so a full profile starts from the empty
  set. `create` takes an optional `provided` set for synthetic inputs, so one stage can run on
  fixture fields without its upstream stages (R4). A stage id may appear only once in a
  profile, because it keys the RNG. Unknown stages are `not_found`; a violation is
  `failed_precondition` naming the stage and the missing fields. After a successful stage the
  runner checks that every declared output field exists (`internal` otherwise); reading
  undeclared fields is not policed at run time.
- **Attempts restart from scratch with a new key.** Attempt a builds a fresh `WorldState` on
  the parameters' grid (plus a copy of the run's initial state, if any) and runs every stage
  with `stage_key(seed, id, a)` and the same `GenParams`. The attempt cap is
  `checks.max_attempts`. A rejection appends a `RejectionRecord {attempt, stage, check,
  measured, required}` to the provenance; exhausting the attempts returns a `FailureReport`
  with code `failed_precondition` and every record.
- **Repair before rejection is a hook.** When `run` returns `reject`, the runner calls
  `Stage::repair` once on the state as `run` left it. `repaired` (or `ok`) lets the attempt
  continue and is logged as a warning; anything else stands. Stages may also repair inline and
  return `repaired` themselves. Either way the record shows the check and its values.
- **Cancellation is cooperative.** The runner polls the cancel flag before every stage; stages
  pass it to `parallel_for`, which skips the remaining chunks. A stage that sees it returns
  `cancelled` or an error with code `cancelled`; the run then returns a `FailureReport` with
  code `cancelled`.
- **Snapshots are a hook, not a policy.** `SnapshotHook::on_stage` runs after every completed
  stage, with the state's provenance already naming that stage and `final` set after the last
  stage of the successful attempt. The caller decides what to keep (`--snapshots
  none|final|all`); A09 writes the `.wgw` files. A failing hook stops the run with its status.
- **Metrics are diagnostics.** Every stage run of every attempt yields a `StageRecord` with
  wall time, `bytes_owned()` afterwards and the stage's own named values. Wall time varies
  between runs, so metrics never enter a hash or the field data.
- **Progress restarts per attempt.** The runner reports scope `"pipeline"` as completed stages /
  stage count. A new attempt starts again at 0, and the `ProgressSink` contract says so.
- **Profiles are data.** `load_profile` / `load_profile_file` read `{schema_version, name,
  description?, stages}` (`schema/profile.schema.json`), with ids `[a-z][a-z0-9_]*` and
  issues reported with their JSON path. `data/profiles/classic.json` and `physical.json` arrive
  with their stages; until then the mock profile in `tests/fixtures/profiles/` is the only one.
- **No global registry.** `StageRegistry` maps ids to factories and checks that a factory makes
  a stage with its own id. The library will offer functions that add its stages; tests build
  registries of mock stages.

## Consequences

- A stage's id, inputs and outputs are part of the profile contract: renaming an id changes
  every world (it keys the RNG), and a new input must be produced earlier in every profile
  that uses the stage, or `create` refuses the profile.
- Every attempt recomputes from stage 1. Caching the stages before the first rejecting one
  would be faster but needs a stage-level invariant (no stage reads the attempt key of a later
  one) that is not worth fixing before profiling (A30) shows rejections are costly.
- Field lifetimes (releasing fields no later stage reads, plan section 3) are not managed yet;
  the runner keeps every field. The physical profile's N_sim grid adds a grid override when
  A23 needs it.
- `Pipeline` is move-only and runs one world at a time; concurrent runs use separate
  pipelines and executors (plan 6.6).
