# ADR 0018: Biomes, regions and the stages C4, C8 and C9

- Status: accepted
- Date: 2026-10-25
- Work package: A20

## Context

R§9 makes the biome a pure function of the local fields (z, P, D, T, salinity). Elevation decides first. Rainfall × drainage then gives a base biome (table R§9.2), and temperature and salinity select variants (R§9.1). R§9.3 groups cells by region type (forest, swamp, grassland, …) into 8-connected regions that carry an area, a centroid, the set of fine biomes, a size class (small 1–24 cells, medium 25–99, large ≥ 100) and a name. R§11 checks REGION_COUNTS after the first vegetation pass and in the final verification, and SUBREGION_MAX in the final verification.

Several pieces have existed since A07: the decision table `data/tables/biome_table.json` (`params::BiomeTable`, with its coverage rules and confidence flags), the fields `biome` and `region_id`, `core::Region` with its layer and JSON round-trip, and the parameters `checks.REGION_COUNTS` and `checks.SUBREGION_MAX`. A20 adds the kernels in `wg::classify` (`include/wg/classify/`). It also adds the classic stages that A14, A18 and A19 left open: C4 (first vegetation), C8 (climate recomputation) and C9 (biomes and regions). The report leaves the details below open. Each one changes every world.

## Decision

- **Compiled table.** `BiomeClassifier::create` turns the table into lookup tables: band by temperature, base row by (P, D), variant by (base, band, saline), and water biomes by band and salinity. It refuses a table with a gap or an overlap, so `classify` cannot fail. The loader already rejects such tables. The classifier repeats the check because it indexes the lookup tables directly.
- **Rounding.** Each value is rounded down before it is compared with the integer bounds of the table, as the bands do ("temperature rounded down"). P and D are clamped to [0, 100] and T to [−1000, 1000], and NaN counts as the lower bound. z ≤ 99 is then the same test as z < 100 = `terrain::kSeaLevel`, and a rainfall of 9.99 still belongs to the row 0–9.
- **Water first.** With the `water` field (C9), an ocean cell gets the ocean biome of its band and a lake cell gets the lake biome of its band and salinity. This happens before the mountain test, so a mountain lake stays a lake. Without `water` (C4), ocean is z ≤ `ocean_max_elevation` and there are no lakes. A land cell below sea level that `water` calls land stays land: `water` is the authority once it exists.
- **Rivers stay land.** River cells keep the biome of their fields. DF draws rivers across region tiles, and river biomes would cut a region along every channel. `river_biome(T, s)` gives the RIVER_* biome for the export and the local map. The biome field never holds it.
- **Order on land.** First mountain (z ≥ 300). Then the frozen band: tundra if D < 75, glacier otherwise. Then the base row and its variant. Mountains win over freezing, as R§9.1 lists them.
- **Salinity.** A wetland is saline when salinity ≥ `saline_min_salinity` (66), so brackish (33) counts as fresh. Dry biomes ignore salinity. Lakes look up the largest class value listed in the table that is ≤ the cell's salinity.
- **Mangrove.** The mangrove biome replaces the variant when the base is `mangrove.base` (swamp), the band is `mangrove.band` (tropical), the cell is saline and D ≤ `mangrove_max_drainage` (9), as in R§9.1.
- **Hills.** No biome has the region type `hills`. It comes from the base rows with `hills: true` (rainfall 10–65, drainage ≥ 66). A land cell on such a row keeps its vegetation biome (grassland, savanna, shrubland), but its region type is `hills`. That is how REGION_COUNTS.HILLS counts anything. Frozen cells and mountains on those rows keep their own type.
- **Regions cover every cell.** Ocean and lake components are regions too, so `region_id` is set everywhere and the region areas sum to N². The acceptance test reads this way: the regions of land types sum to the land area. Regions join 8-neighbours of equal type, including both pairs of a diagonal crossing, as in DF. They are numbered 0 … K−1 in row-major order of their first cell (the union-find root is the smallest cell). The centroid is the mean column x and row y, computed from exact integer sums. `biomes` is ascending. The size class is the last `region_sizes` entry whose `min_tiles` ≤ area. Without a namer, `name_id` is the region id.
- **Checks.** `check_region_counts` maps the nine REGION_COUNTS members to region types by name. The initial phase reads [0] as cells (the report's "squares") and [1] as regions; the final phase reads [2]. The first failure in parameter order wins, with the check id `REGION_COUNTS.<TYPE>[k]`. `check_subregion_max` fails above SUBREGION_MAX, counting every region including ocean and lake regions. C11 (A22) runs the final phase and SUBREGION_MAX.
- **C4** (`c04_vegetation`) computes vegetation without water. It then writes the provisional biome, without water or salinity, labels the regions into scratch memory and rejects on the initial REGION_COUNTS. The temperature it sees has no lapse term yet (ADR 0012). C8 and C9 correct both.
- **C8** (`c08_climate`) runs on the final terrain, in this order:
  1. rainfall in place (`orographic_precipitation`, ADR 0016);
  2. temperature in place: C2's T₀ + T_φ plus the lapse term, the forest term of C4's vegetation and the rain term of the new rainfall, with no second latitude term (ADR 0012);
  3. vegetation from the new temperature and rainfall, with `water`;
  4. salinity with the estuaries (ADR 0017);
  5. the lake classes (`assign_lake_salinity`).

  Every lake cell's salinity is then set to its lake's class, so the field and the `lakes` layer agree, and a lake gets one lake biome in C9. The forest term uses C4's vegetation, not the recomputed one: the causal order is climate → vegetation, and one pass needs no iteration.
- **C9** (`c09_biomes`) classifies with `water` and `salinity`, then writes `biome`, `region_id` and the `regions` layer. It has no checks.

## Consequences

- No parameters, fields or data tables change. The new stages change the world hash of every profile that includes them. The A12–A17 test profiles stay as they were. `tests/fixtures/profiles/classic_biomes.json` runs C1 to C9.
- Stages C4, C8 and C9 need the data tables (`StageContext::tables`). Without them they fail with invalid_argument. `tests/support/stage_run.hpp` can now pass tables, and `run_classic` runs a profile with the tables of `data/`.
- The internal helpers `write_field`, `read_field` and `data_tables` of `src/stages/classic/classic_stages.hpp` replace the copies in C6.
- C11 (A22) uses `count_region_types`, `check_region_counts(final)` and `check_subregion_max` on the C9 layer.

## Alternatives considered

- **River biomes in the field.** This is closer to the table, but it splits regions along every river, and the report's region types have no rivers.
- **Regions of land types only.** This would match the acceptance wording literally, but REGION_COUNTS has an OCEAN entry, and DF names its seas.
- **Lake biome by per-cell salinity.** This keeps C8 unchanged, but a coastal lake would then mix fresh and brackish biomes. The lake class of A19 already exists.
- **C8 iterates vegetation and temperature to a fixed point.** It costs passes for a small correction that the report does not ask for.
- **Rounding to the nearest value.** It shifts every bound by half a unit against the table's own rule for temperature.
