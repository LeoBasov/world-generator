# ADR 0004: Conventions of the numerics kernels

- Status: accepted
- Date: 2026-10-21
- Work package: A06

## Context

A06 adds the shared numerical building blocks in `wg::num` (`include/wg/numerics/`,
`src/numerics/`): neighbourhoods, masked Laplacian, BFS and exact distance transforms, union–find
labelling, a heap with index tie-break, quantile mapping, histograms, radial power spectrum,
variogram, D8 slope and central differences. The hydrology, terrain and climate stages and the
statistical tests all build on them. Several behaviours are not fixed by the plan but must be
the same in every kernel: how a grid or mask edge is treated, how ties are broken, which units
a result has, and which results must be bit-identical across platforms (requirement R1).

## Decision

- **Kernel shape.** Kernels take input spans, a `core::Grid`, at most one params struct,
  caller-owned output spans and a `core::Executor`; they return `Status` or `Expected`. Every
  span must hold one value per cell (`invalid_argument` otherwise). A mask is a
  `std::span<const std::uint8_t>`; an empty mask means every cell. Scratch memory comes from an
  injected `core::MemoryResource` through a `core::Arena`.
- **Edges.** The grid is not periodic. A neighbour that lies off the grid or outside the mask is
  replaced by the centre value (zero flux, Neumann). So the masked Laplacian has no flow across
  the mask edge, and smoothing conserves the sum over the mask.
- **Units.** Results are per cell in field units, except `d8_slope`, which uses `Grid::dx_m`
  and `Grid::hu_m` and is dimensionless (m/m). Distances are in cells.
- **Ties.** Neighbours are visited in `core::kDir8` order and the first of equal candidates
  wins (D8 slope direction). The heap orders by (key, cell index). Quantile ranks order by
  (value, index). Union–find makes the smaller root the new root, so a component's root is its
  first cell in row-major order and labels are numbered in that order.
- **Parallelism.** Loops over cells run as `core::parallel_for` over rows with a constant grain
  of 16 rows (`detail::kRowGrain`); flat loops use 65536 values. Each chunk writes only its own
  outputs or its own partial result, and partials are combined in chunk order (ADR 0003). Every
  grid kernel therefore gives bit-identical results for any executor and thread count.
- **Exact distances.** The EDT follows Felzenszwalb and Huttenlocher (2012). Squared distances
  are integers below 2^53 and the envelope boundaries come from one correctly rounded division,
  which cannot move a boundary across an integer position. The result is exact and equals a
  brute-force search.
- **Spectral diagnostics.** The FFT and `radial_power_spectrum` compute twiddle factors and the
  Hann window with `std::cos` and `std::sin`, whose last bits differ between platform libms.
  Their results are deterministic on one platform but not bit-identical across platforms. They
  serve the statistical tests (plan section 10.1) and analysis only. No stage may write them
  into a world field or a golden hash. The spectrum uses the `(n - 1)^2` block, a power of two,
  removes its mean, applies a 2-D Hann window and normalises by the sum of the squared weights,
  so white noise of variance s^2 has level s^2.

## Consequences

- Stages can combine kernels without border special cases, and a mask edge acts like a grid
  edge.
- Changing a grain, a tie rule or the neighbour order changes outputs and golden hashes, so it is
  an intended output change (`golden/**` branch).
- The white-noise flatness test uses octave bands averaged over 8 fields: with a Hann window a
  single radial bin holds too few independent modes for a ±10 % bound.
- If a stage ever needs a spectral result inside the world, it needs a deterministic FFT
  (twiddles from `det_*` functions or exact tables) first.

## Alternatives considered

- **Periodic (wrap-around) grids.** They make spectra cleaner, but worlds are bounded maps and
  the hydrology needs real edges.
- **Ghost cells set to zero at edges (Dirichlet).** This pulls values toward zero at the mask edge
  and breaks conservation of the smoothed mass.
- **An external FFT library.** It would be a new dependency for a diagnostic, and its results
  would not be bit-identical across platforms either.
- **`std::vector` for scratch.** It is simple, but bypasses the injected `MemoryResource` that
  plan section 6.3 requires for large buffers. Small result vectors (histogram counts, spectra,
  variograms) still use `std::vector`, with allocation failures turned into `out_of_memory`.
