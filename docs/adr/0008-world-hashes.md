# ADR 0008: World and stage hashes

- Status: accepted
- Date: 2026-10-23
- Work package: A10

## Context

Golden tests (plan section 10.3) compare hashes of generated worlds per toolchain, and Phase 2
compares worlds generated in headless Godot against `wgcli` (hash parity). `wgcli hash` (A10)
is the first producer of these hashes, so their definition must be fixed before any golden file
exists. A hash that covered the world file bytes would change with the zstd level, the metadata
JSON layout or the library version, none of which change the world.

## Decision

- **Field hash:** XXH3-64 (seed 0) of the field's raw values as stored in a world-file chunk:
  row-major, little-endian, in the registry dtype.
- **World hash:** XXH3-64 of the UTF-8 text

  ```
  grid <n>\n
  <field name> <field hash>\n     for every present field, in FieldId order
  layers <XXH3-64 of io::layers_to_json(layers)>\n
  ```

  with hashes as 16 lower-case hex digits. Printed as `0x` followed by those digits.
- **Excluded:** metrics and wall times, the provenance (profile, seed, attempt, rejections),
  the parameter hash and the library version. The seed and parameters determine the world, so
  any change of theirs shows in the fields anyway; the provenance can be compared separately.
- **Stage hashes** (`--per-stage`) are world hashes of the state after each stage of the
  successful attempt, taken by a snapshot hook. The last one equals the world hash.
- A generated state and the world file written from it hash the same (tested in
  `tests/unit/wgcli/test_world_hash.cpp`).

## Consequences

- Changing the encoding of `layers_to_json` (number formatting, key order) or a field's dtype
  changes every world hash, as it should for a format change; both need a golden update.
- Hashes depend on little-endian storage, which every supported platform has.
- Adding a field to the registry does not change the hash of worlds that do not hold it.
