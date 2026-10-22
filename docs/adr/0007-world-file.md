# ADR 0007: World file and export

- Status: accepted
- Date: 2026-10-22
- Work package: A09

## Context

A09 adds the world file `.wgw` of plan section 6.5 (header, chunk table, independently
compressed field chunks, a JSON metadata chunk), snapshots, and PNG16 / EXR / JSON export.
Its acceptance tests: round trip bit-identical; reading one field touches only its chunk; a
corrupted chunk is detected by XXH3; a future-version file is refused; PNG16 values equal the
scaled field. Plan 6.6 requires every loader to accept in-memory buffers as well as paths,
because exported Godot builds read packed data through `FileAccess`. Section 6.5 fixed the
byte sizes of header and table entry but not every detail; `docs/file_format.md` now does.

## Decision

- **Reads go through `ByteSource`.** `WorldReader` asks a `ByteSource` (`size`, `read_at`) for
  exactly the ranges it needs: header, table, metadata on `open`, then one range per field.
  `MemorySource` and `FileSource` ship with the library; the Godot binding wraps `FileAccess`.
  The partial-read test records the ranges.
- **Chunk table at the end.** The writer streams header placeholder, field chunks, metadata,
  table, then patches the header, so writing a file needs memory for one chunk, not the world.
  The table must end the file exactly, which also catches truncation and appended bytes.
- **Metadata is a table entry** named `$metadata` with dtype 255, so it is located, checksummed
  and compressed like a field. It holds the provenance, the full `params.json` document and
  every vector layer as JSON. The seed and parameter hash also sit in the header for tools that
  read only 64 bytes; both copies must agree.
- **The writer checks consistency.** `params_hash(params)` must equal the provenance's hash;
  non-finite layer or rejection values are refused, since JSON cannot hold them and the round
  trip must be exact. Floats go through the shortest round-trip decimal.
- **zstd at a fixed level, raw fallback.** Default level 3, single-threaded, no assembly: file
  bytes are reproducible for a given zstd version. A chunk zstd does not shrink is stored raw.
  Golden hashes (A10) hash field contents, never file bytes, so a zstd upgrade cannot break
  them.
- **Strict reader, tolerant of new fields.** Bad magic, flags, padding, reserved bytes,
  unknown codecs, overlaps, size or dtype mismatches and duplicate names are `parse_error`; a
  newer format or metadata version is `unsupported_version`. Chunks named after fields this
  library does not know are skipped and listed, because field ids are append-only (A02).
- **Atomic file writes.** `write_world_file` writes `<path>.tmp` and renames it over the target.
- **Snapshots are files written by a hook.** `SnapshotWriter` implements A08's `SnapshotHook`
  with the policies `none`, `final` and `all` and names files `a<attempt>_<index>_<stage>.wgw`.
- **Export is for review, never read back.** PNG16 is 16-bit grey (lodepng, no auto-convert),
  row 0 at the top, `round((x - lo) / (hi - lo) * 65535)` clamped; the range is explicit, else
  the registry range, else the identity for u8 / u16 fields, else the data's min and max; it is
  stored in `tEXt` chunks. EXR (tinyexr, optional through `WG_WITH_EXR`) is one float32 channel
  `Y`, ZIP-compressed, values unscaled. The layers JSON equals the metadata's `layers`.
- **Third-party code is compiled into the archive.** zstd, lodepng and tinyexr (with miniz)
  have no usable CMake targets for this purpose, so their sources are object libraries whose
  objects go into `worldgen_core`. The installed package needs no extra dependencies, and the
  sources get none of our warning flags.

## Consequences

- A world file is self-contained: `wgcli inspect`, `compare` and `export` (A10) and Godot
  (B10) need nothing else to interpret it.
- The parameters in a file must validate in the reading library. A parameter schema change that
  makes old values invalid makes old files unreadable until a migration exists.
- A field chunk is read into its final buffer (an adopted Godot buffer, too) with one
  allocation for the stored bytes; raw chunks need none.
- Symbols of zstd, lodepng and miniz are part of the static library. A consumer that links its
  own copies may see duplicate symbols; renaming them is possible if it ever matters.
- zstd needs a C compiler, so the project now enables C.

## Alternatives considered

- **Table after the header** (needs every compressed size before the first chunk is written,
  so the whole world in memory) — rejected for 2049² worlds.
- **Metadata at a fixed location outside the table** — no checksum or codec without
  special-casing; the table entry gives both for free.
- **Binary vector layers** — smaller, but the plan chose JSON (6.1) and the layers are tiny
  next to the fields.
- **zstd's own CMake build through FetchContent** — adds its targets to our export set and
  installs its headers; the object-library route keeps it private.
- **Data-range scaling for every PNG** — images of the same field would not be comparable across
  worlds; the registry range keeps them comparable where one exists.
