# ADR 0002: Counter-based RNG with a frozen layout and our own exp/log

- Status: accepted
- Date: 2026-10-21
- Work package: A04

## Context

Requirement R1 (plan section 2) asks for bit-identical worlds for the same seed, parameters,
profile, library version and platform, for any thread count. The plan fixes the shape of the
random numbers, u_{i,k} = 2^-53 floor(H(sigma_{s,a} || i || k) / 2^11) with
sigma_{s,a} = H(sigma || s || a), and leaves the hash H and the meaning of "||" open. It forbids
`std::` distributions, which use different algorithms in libstdc++, libc++ and MSVC (section 6.3).

The A04 acceptance test also requires the draws to be identical on all platforms. Normal,
log-normal and Poisson sampling need exp and log. `std::exp` and `std::log` come from the
platform libm (glibc, the MSVC CRT, Apple libm), and these differ in the last bit for some
arguments.

## Decision

1. **Hash.** H is the SplitMix64 finaliser `mix64`. Appending a 64-bit word is
   `combine(h, w) = mix64(h ^ mix64(w + 0x9E3779B97F4A7C15))`. Identifiers (stage ids, salts by
   name) enter as their FNV-1a 64 hash. Then
   `stage_key(seed, id, a) = combine(combine(mix64(seed), fnv1a(id)), a)` and
   `bits64(key, i, k) = combine(combine(key, i), k)`.
2. **Uniforms.** `uniform01` keeps the top 53 bits, exactly as in the plan. `uniform_open01`
   keeps the top 52 bits plus 1/2, so it lies in (0, 1) and the addition is exact.
3. **Streams.** Processes with a variable number of draws use an `RngStream`, a local
   (key, id, draw counter) value whose k-th draw is `bits64(key, id, k)`. Nothing is shared.
4. **Deterministic math.** `det_exp`, `det_log` and `log_factorial` (`wg/core/detmath.hpp`)
   are ports of FreeBSD msun's fdlibm algorithms. They use only IEEE + - * /, sqrt and bit
   operations, so with `-ffp-contract=off` / `/fp:precise` they give the same bits everywhere.
   All distributions use them instead of `std::exp` / `std::log`.
5. **Distributions.** Normal draws use inversion (Wichura's AS 241) of one open uniform.
   Log-normal is `det_exp` of a normal draw. Poisson uses inversion below lambda = 10 and
   Hoermann's PTRS above it. Weighted bins search a float64 cumulative sum built in index order.
6. **Reference.** `tools/rng/rng_reference.py` re-implements all of the above bit for bit.
   `tools/rng/gen_rng_kat.py` writes the known-answer vectors `tests/unit/core/rng_kat.inc`
   from it, and CI checks that the file is current.

The layout is frozen from now on. Changing a constant, the combination order or a sampling
algorithm changes every world and every golden hash, so such a change needs a new ADR and a
`golden/**` branch (plan section 10.3).

## Consequences

- Every draw is a pure function of its arguments, so parallel kernels are deterministic for any
  schedule, and stages can be tested in isolation (R4).
- The known-answer tests pass on the GCC, Clang, MSVC and Apple Clang runners, which shows the
  draws are the same on every platform. The golden hashes are still kept per toolchain
  (section 6.8), because kernels in later packages may use `std::` math.
- Kernels that need bit-identical exp or log across platforms can call `det_exp` / `det_log`.
  They are somewhat slower than libm.
- `hash_id` is FNV-1a rather than XXH3, because xxHash is only added with A09. Stage ids are
  short and fixed, so FNV-1a's weaker mixing does not matter here: `combine` mixes its output
  again.

## Alternatives considered

- **PCG-RXS-M-XS output function as H** (named in the plan). It has similar quality, but
  SplitMix64 is simpler and its published output sequence gives an independent check.
- **`std::exp` / `std::log`.** These are simpler and faster, but only the uniform and
  weighted-bin draws would then be identical across platforms, which fails the A04 acceptance
  test.
- **Box–Muller or the Marsaglia polar method for normal draws.** Box–Muller needs cos and sin,
  and the polar method uses a variable number of draws, which rules out the stateless
  (key, cell, draw) form. Inversion needs one draw and only log and sqrt.
- **A stateful generator per thread.** Its results depend on scheduling, which violates R1.
