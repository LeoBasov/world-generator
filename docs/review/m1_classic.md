# Visual review: classic profile (A23, gate M1)

Review checklist of plan section 10.3, test level 7, for the classic profile at A23.

- Date: 2026-10-25.
- Commit: the A23 branch, with the failed-basin memo of ADR 0021.
- Worlds: `classic`, N = 257. Seeds 1–6 with the schema defaults, and seeds 1–3 with
  `df_default`. All nine finished in their first attempt.
- Images: `python tools/review/review_set.py --wgcli <wgcli> --out review --seeds 1..6` (add
  `--params df_default` for the second set). That makes `index.html` with elevation (terrain map,
  registry range), plus water, rainfall, temperature, drainage, vegetation, salinity, biome and
  region id (viridis, data range). `golden-update.yml` uploads the same set for seeds 1–6 as
  before/after artifacts.

The images are not committed. Where an item can be measured, the measurement backs the visual
judgement. The scripts used are described with each item.

| Checklist item | Result | Evidence |
|---|---|---|
| Rivers end in sea or lake | Pass | `classic.profile runs end to end` (`tests/integration/classic/test_classic.cpp`): the receiver of every river mouth is an ocean or lake cell of the final `water` field, at N = 33 and 65. `c06_rivers` also runs `check_river_graph` on every world. The images show no river ending on land. |
| Axis-aligned creases of midpoint displacement not dominant after erosion | Pass | No visible creases in the elevation images. Mean \|Δz\| across cell boundaries on the edges of 8-, 16-, 32- and 64-cell blocks is 0.82–0.98 times the mean over all boundaries, in all six default seeds. Creases would raise it above 1. |
| Rain shadows on the lee side of ranges | Pass | Mean rainfall on land 1–8 cells west (windward) and east (lee) of mountain cells (z ≥ 300), with the wind from the west: seed 1 74 vs 34, seed 2 55 vs 27, seed 3 44 vs 40, seed 4 47 vs 46, seed 5 55 vs 29, seed 6 70 vs 33. Windward is wetter in every seed. The shadow is weak in seeds 3 and 4, whose ranges are small and broken. |
| No tile-aligned biome seams | Pass | The share of biome changes across block-edge boundaries is 0.84–1.14 times the share across all boundaries (8- to 64-cell blocks, six seeds). That is no excess at the seams of the fractal mesh, and none is visible. |
| Climate classes vary smoothly with latitude and elevation | Pass | Temperature falls smoothly from the warm south edge to the pole on the north edge (`poles` = north), with cold spots on every mountain range. Vegetation follows it, with none on ice and high mountains. No banding at class boundaries. |
| Mountain belts follow convergent boundaries | n/a | Physical profile only (A29). |

## Observations outside the checklist

- **Dense, straight river networks.** Every test-river source still on a mountain edge after
  erosion becomes a river: about 1,400–2,000 rivers per world at N = 257. On the flat lowlands
  between ranges, D8 routing over the ε-filled surface draws them as long straight lines and
  parallel diagonals. It is a cosmetic issue, not a correctness one, since the rivers drain
  correctly. Two candidates for a later package: draw only rivers from width class 2 (stream)
  up, or route over flats by distance to the outlet instead of ε steps.
- **Saturated orographic rain.** On some windward slopes rainfall reaches its clamp of 100,
  which shows as flat patches in the rainfall map (for example seed 1). This is the A18 model with
  default `orographic_gain`; calibration belongs to A31.
- The `small_island` preset needs about 24 attempts per seed at N = 65 (PEAK_NUMBER_MIN).
  The golden case `classic_small_island` relies on that to exercise the attempt loop.
