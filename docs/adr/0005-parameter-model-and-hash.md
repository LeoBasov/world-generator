# ADR 0005: Parameter model, data tables and their hashes

- Status: accepted
- Date: 2026-10-21
- Work package: A07

## Context

A07 adds the generation parameters (every R§3 / R§11 parameter plus the physical ones), their
JSON loader with cross-field validation, presets, the data tables of R3 (biome, climate,
salinity, rock, soil, mineral) and the parameter hash written into every world file. Three
consumers need the same parameter model: the C++ library, `wgcli validate` (A10) and the Godot
parameter editor (B03, "GenParams resource generated from the parameter schema"). The hash
keys golden files and world files, so its definition must not drift silently.

## Decision

- **The schema is the source.** `schema/params.schema.json` (JSON Schema 2020-12) lists every
  parameter with type, inclusive bounds, default, description and the annotations `x-df-name`,
  `x-unit` and `x-report`. `tools/codegen/gen_params.py` (standard library only) turns it into
  the committed header `include/wg/generated/gen_params.hpp`: nested structs whose member
  initialisers are the defaults (`GenParams{}` is the default parameter set) and the descriptor
  table `kParamDescriptors` (path, kind, bounds, defaults, labels, DF name, member accessor) in
  schema order. CI regenerates the header and fails on a difference, as for the field registry.
  Fixed-length arrays and string enums are supported; reals are `double`, integers
  `std::int32_t`. A `$ref` to `$defs` becomes a shared struct; a `default` object next to the
  `$ref` sets that occurrence's defaults.
- **Files list only what differs.** A missing parameter takes its default; unknown keys, wrong
  types and values out of bounds are errors with their JSON path. `schema_version` is required;
  a newer version is `unsupported_version`. Cross-field rules (field ranges inside the field
  registry, min <= max, 2^n + 1 grid, ordered pairs, square counts) run when the structure is
  clean. Known contradictions of the report (peaks without elevation 400, volcanoes without
  volcanism 100, rivers below elevation 104, edge oceans without elevation below 100) are
  warnings: the check makes every attempt fail, but the set is accepted.
- **Seed and profile are run defaults.** `params.json` may carry `seed` and `profile`; they live
  in `ParamsDocument` next to `GenParams` and are not part of the parameter hash, because the
  world file stores the seed separately and wgcli overrides both.
- **The hash is over values.** `params_hash` is XXH3-64 (seed 0) of a canonical encoding:
  the tag `wg.params`, the schema version, then for every descriptor in schema order its path,
  kind and count followed by its values: reals as the little-endian bits of the double (-0.0 as
  +0.0, NaN as one quiet NaN), integers as little-endian int64, booleans and enumerations as one
  byte. Formatting, key order and omitted defaults do not change it, and `to_json` writes
  doubles that read back exactly, so a JSON round trip keeps the hash. `tables_hash` encodes
  every table value the same way.
- **Tables are data with rules.** Each table has its own JSON Schema; the C++ loaders also check
  what JSON Schema cannot: the base biome rows partition rainfall x drainage on 0..100 exactly
  once, every base label has a variant per temperature band (fresh and saline for wetlands),
  the temperature bands partition -1000..1000, salinity class values equal the field registry,
  biome ids are append-only (id = position), and minerals name existing rocks. Rows carry a
  confidence flag (`documented`, `observed`, `unverified`).
- **JSON stays inside the library.** nlohmann/json and xxHash are private build dependencies;
  `src/io/json.cpp` is the only translation unit that calls `parse` and `dump`, and catches
  their exceptions. Loaders accept buffers and paths (plan 12.1).

## Consequences

- Adding, removing or renaming a parameter changes every parameter hash, and with it every
  golden hash. Until the API freeze at M1 that is accepted; afterwards a parameter change needs
  a schema_version bump with a migration in the loader.
- The encoding is frozen by the golden hashes of A10. A unit test checks that the hash changes
  with every single parameter element and is unchanged by a JSON round trip.
- Table thresholds marked `unverified` (the R§9.2 gap at rainfall 10-32 x drainage 0-32, filled
  with grassland, and the grassland/savanna/shrubland edges) are revisited at M1.
- Map wrap (plan 16) is not a parameter yet; it arrives with its decision before A11.
