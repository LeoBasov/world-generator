# World file format `.wgw`

Version 1. Written and read by `wg::io` (`include/wg/io/world_file.hpp`); the decisions behind
it are in ADR 0007. Plan section 6.5 is the source.

A world file holds one world: the raster fields of a `WorldState`, its vector layers, its
provenance and the full parameters it was made with. A **snapshot** is the same file written
after a stage; its provenance names the last completed stage.

## Layout

All integers and floats are **little-endian**. Floats are IEEE 754.

```
offset 0             header (64 bytes)
offset 64            field chunks, one per present field, in ascending field id
                     metadata chunk
chunk_table_offset   chunk table (68 bytes per entry), the last bytes of the file
```

The chunk table ends the file exactly: `chunk_table_offset + chunk_count * 68` is the file
size. Chunks lie between the header and the table and do not overlap; the writer places them
back to back in table order.

## Header (64 bytes)

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | magic | `57 47 57 00` (`"WGW\0"`) |
| 4 | 2 | format version u16 | 1. A reader refuses a higher version (`unsupported_version`) and 0. |
| 6 | 2 | flags u16 | 0 in version 1; nonzero is refused |
| 8 | 4 | grid n i32 | cells per axis, 2^k + 1 |
| 12 | 8 | dx_m f64 | cell size [m] |
| 20 | 8 | hu_m f64 | metres per elevation unit |
| 28 | 8 | seed u64 | as in the provenance |
| 36 | 8 | params hash u64 | `params_hash` of the stored parameters (ADR 0005); as in the provenance |
| 44 | 4 | library version u32 | of the writer: `major << 20 \| minor << 10 \| patch` |
| 48 | 4 | chunk count u32 | table entries, metadata included |
| 52 | 8 | chunk-table offset u64 | |
| 60 | 4 | padding | 0 |

## Chunk table entry (68 bytes)

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 32 | name | ASCII `!`..`~`, zero-padded; the field name of the registry, or `$metadata` |
| 32 | 1 | dtype u8 | `DType` of the field (0 u8, 1 u16, 2 i32, 3 f32); 255 for the metadata |
| 33 | 1 | codec u8 | 0 raw, 1 zstd; others are refused |
| 34 | 2 | reserved u16 | 0 |
| 36 | 8 | raw size u64 | bytes after decompression |
| 44 | 8 | stored size u64 | bytes in the file; equals raw size for codec 0 |
| 52 | 8 | offset u64 | of the stored bytes |
| 60 | 8 | XXH3 u64 | XXH3-64 (seed 0) of the raw bytes |

Each name appears once, and exactly one entry is `$metadata`.

## Field chunks

One chunk per raster field present in the world, row-major (`i = y * n + x`, row 0 the north
edge), `n * n` values of the field's registry dtype, little-endian. Each chunk is compressed on
its own as a single zstd frame, so one field can be read without touching the others. A chunk
that zstd does not make smaller is stored raw (codec 0).

A reader checks that a known field's dtype matches the registry and that its raw size is
`n * n * dtype size`. Chunks named after fields the reader's registry does not know (appended
by a newer library; field ids are append-only) are skipped and listed, never refused.

## Metadata chunk

UTF-8 JSON, compact, with these keys:

```json
{
  "schema_version": 1,
  "provenance": {
    "profile": "classic",
    "seed": 42,
    "params_hash": "89abcdef01234567",
    "library_version": "0.1.0",
    "attempt": 1,
    "completed_stages": ["c01_allocate", "c02_seed"],
    "rejections": [
      {"attempt": 0, "stage": "c03_fit", "check_id": "PEAK_NUMBER_MIN",
       "measured": 2, "required": 5}
    ]
  },
  "params": { "schema_version": 1, "grid": { "size": 129 } },
  "layers": { "peaks": [], "volcanoes": [], "river_graph": {}, "lakes": [], "regions": [],
              "plates": [], "geo_columns": [], "minerals": [] }
}
```

- `provenance` is `core::Provenance`: `params_hash` as 16 lowercase hex digits,
  `library_version` of the library that ran the pipeline. Its seed and params hash must equal
  the header's.
- `params` is a complete `params.json` document (every parameter, `wgcli`'s format), read with
  `load_params`. It must be valid for the reading library.
- `layers` is `core::VectorLayers`: each struct an object with its member names as keys, the
  river graph as arrays (`cell`, `downstream`, `discharge_m3_yr`, `width_class`, `order`,
  `name_id`) of equal length, enumerations as labels (`continental`, `oceanic`; `convergent`,
  `divergent`, `transform`; `vein`, `cluster`, `cluster_small`, `cluster_one`). Floats are the
  shortest decimal that reads back to the same value, so they round-trip bit for bit. JSON has
  no NaN or infinity; the writer refuses them.
- A higher `schema_version` is refused (`unsupported_version`); unknown keys are errors.

`wgcli export --format json` (A10) writes the same `layers` object, indented.

## Reading

A reader first reads the header (64 bytes), then the table, then the metadata chunk. Each
field is read only when asked for: one read of its stored bytes, decompression to exactly the
raw size, and the XXH3 check. Any mismatch is `parse_error` naming the chunk.

## Versioning

- Additive changes that old readers can skip (a new field) need no version change.
- New metadata keys raise the metadata `schema_version`; layout changes raise the format
  version. Readers migrate older versions or refuse them with a message (plan 6.5).
