"""Bit-exact Python reference of the worldgen-core RNG (A04, plan sections 2 and 6.3).

Mirrors include/wg/core/rng.hpp, src/core/detmath.cpp and src/core/distributions.cpp operation
by operation. Python floats are IEEE binary64 and every arithmetic operation rounds once (no
contraction), so with the library built with -ffp-contract=off / /fp:precise both produce the
same bits. gen_rng_kat.py uses this module to write the known-answer vectors of the C++ tests.

Standard library only (runs on Python 3.8).
"""

import bisect
import math
import struct
from typing import List, Sequence

MASK64 = (1 << 64) - 1
GOLDEN_GAMMA = 0x9E3779B97F4A7C15
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


# ---------------------------------------------------------------------------
# Bit helpers
# ---------------------------------------------------------------------------
def to_bits(x: float) -> int:
    """IEEE bit pattern of a double as an unsigned 64-bit integer."""
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def from_bits(b: int) -> float:
    """Double with the given IEEE bit pattern."""
    return struct.unpack("<d", struct.pack("<Q", b & MASK64))[0]


def _high_word(x: float) -> int:
    """Signed high 32 bits of a double (fdlibm GET_HIGH_WORD into an int32_t)."""
    hi = to_bits(x) >> 32
    return hi - (1 << 32) if hi & 0x80000000 else hi


def _set_high_word(x: float, hi: int) -> float:
    return from_bits(((hi & 0xFFFFFFFF) << 32) | (to_bits(x) & 0xFFFFFFFF))


# ---------------------------------------------------------------------------
# Counter hash and keys (rng.hpp)
# ---------------------------------------------------------------------------
def mix64(z: int) -> int:
    """SplitMix64 finaliser."""
    z &= MASK64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)


def combine(h: int, w: int) -> int:
    """The concatenation H(h || w) of plan section 2."""
    return mix64(h ^ mix64((w + GOLDEN_GAMMA) & MASK64))


def hash_id(text: str) -> int:
    """FNV-1a 64 over the bytes of an identifier."""
    h = FNV_OFFSET
    for byte in text.encode("utf-8"):
        h = ((h ^ byte) * FNV_PRIME) & MASK64
    return h


def seed_key(seed: int) -> int:
    return mix64(seed)


def derive(key: int, salt: int) -> int:
    return combine(key, salt)


def derive_name(key: int, name: str) -> int:
    return combine(key, hash_id(name))


def stage_key(seed: int, stage_id: str, attempt: int) -> int:
    """sigma_{s,a} = H(sigma || s || a)."""
    return combine(combine(seed_key(seed), hash_id(stage_id)), attempt)


def bits64(key: int, cell: int, draw: int) -> int:
    """H(sigma_{s,a} || i || k)."""
    return combine(combine(key, cell), draw)


def to_unit01(bits: int) -> float:
    """2^-53 * floor(bits / 2^11), in [0, 1)."""
    return float(bits >> 11) * 2.0 ** -53


def to_open01(bits: int) -> float:
    """(floor(bits / 2^12) + 1/2) * 2^-52, in (0, 1)."""
    return (float(bits >> 12) + 0.5) * 2.0 ** -52


def uniform01(key: int, cell: int, draw: int) -> float:
    return to_unit01(bits64(key, cell, draw))


def uniform_open01(key: int, cell: int, draw: int) -> float:
    return to_open01(bits64(key, cell, draw))


def uniform(key: int, cell: int, draw: int, lo: float, hi: float) -> float:
    return lo + (hi - lo) * uniform01(key, cell, draw)


def uniform_index(key: int, cell: int, draw: int, n: int) -> int:
    if n == 0:
        return 0
    r = int(uniform01(key, cell, draw) * float(n))
    return min(r, n - 1)


class RngStream:
    """Sequential draws k = first, first + 1, ... of one stream id."""

    def __init__(self, key: int, stream_id: int, first_draw: int = 0) -> None:
        self.key = key
        self.id = stream_id
        self.draw = first_draw

    def next_u64(self) -> int:
        b = bits64(self.key, self.id, self.draw)
        self.draw += 1
        return b

    def uniform01(self) -> float:
        return to_unit01(self.next_u64())

    def uniform_open01(self) -> float:
        return to_open01(self.next_u64())


# ---------------------------------------------------------------------------
# Deterministic exp and log (detmath.cpp, ports of FreeBSD msun e_exp.c / e_log.c)
# ---------------------------------------------------------------------------
LN2_HI = from_bits(0x3FE62E42FEE00000)
LN2_LO = from_bits(0x3DEA39EF35793C76)
INV_LN2 = from_bits(0x3FF71547652B82FE)
EULER_E = from_bits(0x4005BF0A8B145769)
TWO54 = from_bits(0x4350000000000000)
TWOM1000 = from_bits(0x0170000000000000)
TWO1023 = from_bits(0x7FE0000000000000)
O_THRESHOLD = from_bits(0x40862E42FEFA39EF)
U_THRESHOLD = from_bits(0xC0874910D52D3051)

EXP_P1 = from_bits(0x3FC555555555553E)
EXP_P2 = from_bits(0xBF66C16C16BEBD93)
EXP_P3 = from_bits(0x3F11566AAF25DE2C)
EXP_P4 = from_bits(0xBEBBBD41C5D26BF1)
EXP_P5 = from_bits(0x3E66376972BEA4D0)

LOG_LG1 = from_bits(0x3FE5555555555593)
LOG_LG2 = from_bits(0x3FD999999997FA04)
LOG_LG3 = from_bits(0x3FD2492494229359)
LOG_LG4 = from_bits(0x3FCC71C51D8E78AF)
LOG_LG5 = from_bits(0x3FC7466496CB03DE)
LOG_LG6 = from_bits(0x3FC39A09D078C69F)
LOG_LG7 = from_bits(0x3FC2F112DF3E5244)


def det_exp(x: float) -> float:
    hx = (to_bits(x) >> 32) & 0xFFFFFFFF
    xsb = (hx >> 31) & 1
    hx &= 0x7FFFFFFF

    if hx >= 0x40862E42:  # |x| >= 709.78...
        if hx >= 0x7FF00000:
            if math.isnan(x):
                return x + x
            return x if xsb == 0 else 0.0
        if x > O_THRESHOLD:
            return math.inf
        if x < U_THRESHOLD:
            return 0.0

    hi = 0.0
    lo = 0.0
    k = 0
    if hx > 0x3FD62E42:  # |x| > 0.5 ln2
        if hx < 0x3FF0A2B2:  # and |x| < 1.5 ln2
            if x == 1.0:
                return EULER_E
            if xsb == 0:
                hi = x - LN2_HI
                lo = LN2_LO
                k = 1
            else:
                hi = x + LN2_HI
                lo = -LN2_LO
                k = -1
        else:
            k = int(INV_LN2 * x + (0.5 if xsb == 0 else -0.5))
            t = float(k)
            hi = x - t * LN2_HI  # exact
            lo = t * LN2_LO
        x = hi - lo
    elif hx < 0x3E300000:  # |x| < 2^-28
        return 1.0 + x

    t = x * x
    if k >= -1021:
        twopk = from_bits((0x3FF + k) << 52)
    else:
        twopk = from_bits((0x3FF + k + 1000) << 52)
    c = x - t * (EXP_P1 + t * (EXP_P2 + t * (EXP_P3 + t * (EXP_P4 + t * EXP_P5))))
    if k == 0:
        return 1.0 - ((x * c) / (c - 2.0) - x)
    y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi)
    if k >= -1021:
        if k == 1024:
            return y * 2.0 * TWO1023
        return y * twopk
    return y * twopk * TWOM1000


def det_log(x: float) -> float:
    hx = _high_word(x)
    lx = to_bits(x) & 0xFFFFFFFF
    k = 0
    if hx < 0x00100000:  # x < 2^-1022
        if ((hx & 0x7FFFFFFF) | lx) == 0:
            return -math.inf  # log(+-0)
        if hx < 0:
            return math.nan  # log(negative)
        k -= 54
        x *= TWO54  # subnormal, scale up
        hx = _high_word(x)
    if hx >= 0x7FF00000:
        return x + x
    k += (hx >> 20) - 1023
    hx &= 0x000FFFFF
    i = (hx + 0x95F64) & 0x100000
    x = _set_high_word(x, hx | (i ^ 0x3FF00000))  # normalise x or x/2
    k += i >> 20
    f = x - 1.0
    if (0x000FFFFF & (2 + hx)) < 3:  # -2^-20 <= f < 2^-20
        if f == 0.0:
            if k == 0:
                return 0.0
            dk = float(k)
            return dk * LN2_HI + dk * LN2_LO
        r = f * f * (0.5 - 0.33333333333333333 * f)
        if k == 0:
            return f - r
        dk = float(k)
        return dk * LN2_HI - ((r - dk * LN2_LO) - f)
    s = f / (2.0 + f)
    dk = float(k)
    z = s * s
    i = hx - 0x6147A
    w = z * z
    j = 0x6B851 - hx
    t1 = w * (LOG_LG2 + w * (LOG_LG4 + w * LOG_LG6))
    t2 = z * (LOG_LG1 + w * (LOG_LG3 + w * (LOG_LG5 + w * LOG_LG7)))
    i |= j
    r = t2 + t1
    if i > 0:
        hfsq = 0.5 * f * f
        if k == 0:
            return f - (hfsq - s * (hfsq + r))
        return dk * LN2_HI - ((hfsq - (s * (hfsq + r) + dk * LN2_LO)) - f)
    if k == 0:
        return f - s * (f - r)
    return dk * LN2_HI - ((s * (f - r) - dk * LN2_LO) - f)


HALF_LOG_2PI = 0.91893853320467274178  # ln(sqrt(2 pi))
EXACT_FACTORIALS = 10


def log_factorial(k: int) -> float:
    """ln(k!): exact factorials below 10, Stirling series in x = k + 1 above."""
    if k < EXACT_FACTORIALS:
        factorial = 1.0
        for i in range(2, k + 1):
            factorial *= float(i)
        return det_log(factorial)
    x = float(k + 1)
    z = 1.0 / x
    z2 = z * z
    series = 1.0 / 1680.0 - z2 * (1.0 / 1188.0)
    series = 1.0 / 1260.0 - z2 * series
    series = 1.0 / 360.0 - z2 * series
    series = z * (1.0 / 12.0 - z2 * series)
    return (x - 0.5) * det_log(x) - x + HALF_LOG_2PI + series


# ---------------------------------------------------------------------------
# Distributions (distributions.cpp)
# ---------------------------------------------------------------------------
# Wichura (1988), algorithm AS 241, PPND16.
A = [3.3871328727963666080e0, 1.3314166789178437745e+2, 1.9715909503065514427e+3,
     1.3731693765509461125e+4, 4.5921953931549871457e+4, 6.7265770927008700853e+4,
     3.3430575583588128105e+4, 2.5090809287301226727e+3]
B = [1.0, 4.2313330701600911252e+1, 6.8718700749205790830e+2, 5.3941960214247511077e+3,
     2.1213794301586595867e+4, 3.9307895800092710610e+4, 2.8729085735721942674e+4,
     5.2264952788528545610e+3]
C = [1.42343711074968357734e0, 4.63033784615654529590e0, 5.76949722146069140550e0,
     3.64784832476320460504e0, 1.27045825245236838258e0, 2.41780725177450611770e-1,
     2.27238449892691845833e-2, 7.74545014278341407640e-4]
D = [1.0, 2.05319162663775882187e0, 1.67638483018380384940e0, 6.89767334985100004550e-1,
     1.48103976427480074590e-1, 1.51986665636164571966e-2, 5.47593808499534494600e-4,
     1.05075007164441684324e-9]
E = [6.65790464350110377720e0, 5.46378491116411436990e0, 1.78482653991729133580e0,
     2.96560571828504891230e-1, 2.65321895265761230930e-2, 1.24266094738807843860e-3,
     2.71155556874348757815e-5, 2.01033439929228813265e-7]
F = [1.0, 5.99832206555887937690e-1, 1.36929880922735805310e-1, 1.48753612908506148525e-2,
     7.86869131145613259100e-4, 1.84631831751005468180e-5, 1.42151175831644588870e-7,
     2.04426310338993978564e-15]


def _poly7(c: Sequence[float], r: float) -> float:
    """Horner evaluation c[7] r^7 + ... + c[0], highest coefficient first."""
    return ((((((c[7] * r + c[6]) * r + c[5]) * r + c[4]) * r + c[3]) * r + c[2]) * r
            + c[1]) * r + c[0]


def inverse_normal_cdf(p: float) -> float:
    if math.isnan(p):
        return p
    if p <= 0.0:
        return -math.inf
    if p >= 1.0:
        return math.inf
    q = p - 0.5
    if abs(q) <= 0.425:
        r = 0.180625 - q * q
        return q * _poly7(A, r) / _poly7(B, r)
    r = p if q < 0.0 else 1.0 - p
    r = math.sqrt(-det_log(r))
    if r <= 5.0:
        r -= 1.6
        val = _poly7(C, r) / _poly7(D, r)
    else:
        r -= 5.0
        val = _poly7(E, r) / _poly7(F, r)
    return -val if q < 0.0 else val


def normal(key: int, cell: int, draw: int, mean: float, sd: float) -> float:
    return mean + sd * inverse_normal_cdf(uniform_open01(key, cell, draw))


def normal_stream(s: RngStream, mean: float, sd: float) -> float:
    return mean + sd * inverse_normal_cdf(s.uniform_open01())


def log_normal(key: int, cell: int, draw: int, mu: float, sigma: float) -> float:
    return det_exp(normal(key, cell, draw, mu, sigma))


def log_normal_stream(s: RngStream, mu: float, sigma: float) -> float:
    return det_exp(normal_stream(s, mu, sigma))


MAX_POISSON_LAMBDA = 1e12
POISSON_PTRS_MIN = 10.0
POISSON_MAX_K = 2.0 ** 62


def poisson(s: RngStream, lam: float) -> int:
    if not lam > 0.0:  # also NaN
        return 0
    lam = min(lam, MAX_POISSON_LAMBDA)
    if lam < POISSON_PTRS_MIN:
        # Inversion from 0 with a single uniform.
        u = s.uniform01()
        p = det_exp(-lam)
        cdf = p
        k = 0
        while u >= cdf and p > 0.0:
            k += 1
            p = p * lam / float(k)
            cdf += p
        return k
    # Hoermann (1993), PTRS transformed rejection.
    slam = math.sqrt(lam)
    loglam = det_log(lam)
    b = 0.931 + 2.53 * slam
    a = -0.059 + 0.02483 * b
    log_invalpha = det_log(1.1239 + 1.1328 / (b - 3.4))
    vr = 0.9277 - 3.6224 / (b - 2.0)
    while True:
        u = s.uniform_open01() - 0.5
        v = s.uniform_open01()
        us = 0.5 - abs(u)
        kd = math.floor((2.0 * a / us + b) * u + lam + 0.43)
        if kd < 0.0 or kd > POISSON_MAX_K:
            continue
        if us >= 0.07 and v <= vr:
            return int(kd)
        if us < 0.013 and v > us:
            continue
        k = int(kd)
        lhs = det_log(v) + log_invalpha - det_log(a / (us * us) + b)
        rhs = -lam + kd * loglam - log_factorial(k)
        if lhs <= rhs:
            return k


class WeightedBins:
    """Cumulative weights, summed in index order in float64."""

    def __init__(self, weights: Sequence[float]) -> None:
        self.cdf: List[float] = []
        acc = 0.0
        self.last_positive = 0
        for i, w in enumerate(weights):
            acc += w
            self.cdf.append(acc)
            if w > 0.0:
                self.last_positive = i
        self.total = acc

    def sample(self, u: float) -> int:
        i = bisect.bisect_right(self.cdf, u * self.total)
        return min(i, self.last_positive)

    def sample_keyed(self, key: int, cell: int, draw: int) -> int:
        return self.sample(uniform01(key, cell, draw))
