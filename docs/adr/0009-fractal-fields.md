# ADR 0009: Fractal primary fields

- Status: accepted
- Date: 2026-10-23
- Work package: A11

## Context

A11 adds the generator of the six primary fields in `wg::fractal` (`include/wg/fractal/`,
`src/fractal/`), following R§3.2–3.4 of the technical report: weighted control values on a
coarse grid of 2^(m−1) cells per axis, filled by anisotropic midpoint displacement with
σ_k = σ₀·2^(−kH), then clamped to [min, max]. Stage C2 (A12) calls it once per field, and A24
uses it for fBm detail on grids up to N_sim = 2049. The report is a reconstruction, so several
details are not fixed by it. They feed every world and golden hash, so they are recorded here.
The plan also asked for the map topology to be decided before A11.

## Decision

- **Topology.** The map is a bounded square without wrap, as in DF and ADR 0004.
- **Control grid.** M = min(2^(m−1), N − 1) control cells per axis, spaced (N − 1)/M apart.
  The mesh is limited by the grid only for small N (e.g. N = 17 with m = 6, where every point is
  a control point). m = 1 ("mesh ignored") uses the four corners, drawn with equal weights,
  because the report says the weights are then unused.
- **Control values (R§3.3).** Control point (cx, cy) has index c = cy·(M + 1) + cx. With
  key κ = key.derive("control"), draw 0 of stream c picks the bin (`core::WeightedBins` over
  the five weights), and draw 1 gives the position u inside it: v = min + (bin + u)·Δ,
  Δ = (max − min)/5. The index does not depend on N, so grids of different size share their
  control values for the same seed and mesh.
- **Edge pinning.** `pinned_bins` replaces only the bin choice of a control point; draw 1 is
  still used, so pinning leaves every other control value unchanged. `pin_edges` fills it for
  chosen map edges (R§4.1 edge oceans).
- **Displacement.** ξ ~ N(0, 1) from `core::normal(key.derive("displace"), i, 0, 0, 1)` with
  i the cell index of the new point. Each cell is set exactly once, so i is a unique counter.
  When σ = 0 no draw is taken.
- **Levels.** Level k = 0 is the first subdivision of the control grid (spacing s = s₀/2^k,
  half step h = s/2), so the effect of a variance does not depend on the world size. The
  amplitudes are σ^x_k = c·(V_x/3200)·(max − min)·2^(−kH), the same with V_y for σ^y_k, and
  σ^c_k = (σ^x_k + σ^y_k)/2 for centres. 2^(−kH) is computed as `det_exp(−k·H·ln 2)`, not with
  `std::pow`, so the result is the same bits on every platform (R1).
- **Scheme.** Each level has two steps. First the edge step: a point between east–west
  neighbours takes their mean plus σ^x_k ξ, and a point between north–south neighbours takes
  their mean plus σ^y_k ξ. Then the centre step: a cell centre takes the mean of the four edge
  points around it plus σ^c_k ξ. Means are float64, values are stored as float32. Each step is a
  `parallel_for` over rows with a grain of 16 rows; a step only reads points of earlier steps,
  so the result is identical for any executor.
- **Clamping.** `generate_field` clamps to [min, max] last. `diamond_square` returns the field
  before clamping, which is what the spectral test measures.
- **Statistical tolerances** (`tests/unit/fractal/test_fractal_stats.cpp`, fixed seeds):
  - spectral exponent within ±0.3 of 2H + 2, using the mean spectrum of 20 seeds at N = 257
    with m = 1 (measured: within 0.05 for H = 0.3, 0.5 and 0.8);
  - γ_x/γ_y over lags 1–4 strictly increasing in V_x/V_y, within 10 % of 1 for V_x = V_y, below
    0.8 for V_x/V_y = 1/4 and above 1.2 for 4 and 8 (measured 0.70, 0.94, 1.34, 1.39);
  - χ² of the control bins below the 1 % critical value with 4 degrees of freedom (measured
    8.6 < 13.3);
  - two-sample KS distance between the values of N = 129 and N = 257 at mesh 3 below 0.05
    (measured 0.008). Values are spatially correlated, so the iid critical value does not apply.

## Consequences

- The control values, displacement draws and level convention are frozen. Changing any of them
  changes every world, which makes it an intended output change (`golden/**` branch).
- Anisotropy is moderate: γ_x/γ_y reaches about 1.4 for V_x/V_y = 8. With independent noise
  per point, a displaced point also differs from its neighbours across the axis, so lag-1
  differences are partly isotropic. Stronger directional banding would need noise correlated
  along one axis, which the report does not describe. This is checked again in the M1 visual
  review.
- The axis-aligned creases of midpoint displacement (R§3.4) remain; later erosion has to break
  them up.
- C3 repairs can re-run the field with new pins without touching the unpinned controls.

## Alternatives considered

- **Classic diamond–square (square step from the four corners, then diamond step from 3–4
  parents).** This is closer to the report's wording, but the diamond mean mixes both axes, and
  γ_x/γ_y moved only from 0.92 to 0.99 for V_x/V_y from 1/4 to 8. The step from 4 to 8 changed
  the ratio by 10⁻⁴.
- **Diamond step with only the two collinear parents.** This gave 0.88 to 0.99, barely better.
- **Separable refinement (all x midpoints, then all y midpoints).** It is stronger (up to 2.1),
  but biased toward x: 1.23 for equal variances.
- **Uniform ξ ~ U(−1, 1).** It is cheaper and bounded, but its marginal distribution has light
  tails; it was not chosen.
- **East–west wrap.** It would change midpoint displacement, routing and the wind sweep, and
  the A06 kernels are not periodic.
- **Per-half-step decay 2^(−(k+½)H).** This was the fallback if the spectral exponent missed
  its tolerance. It was not needed.
