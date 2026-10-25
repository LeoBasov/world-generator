#!/usr/bin/env python3
"""Benchmark regression check of the main-branch bench job (plan section 6.8, ADR 0021).

Compares the medians of a wg_bench --json result (nanobench's JSON template) with a baseline
state kept between runs of the same runner label. A benchmark more than --tolerance slower
than its baseline gets a strike; a benchmark with a strike in the previous run as well fails
the check. The new state takes the current median as the baseline of every benchmark without
a strike (so a gradual speed-up or a small change is accepted) and keeps the old baseline of a
benchmark with a first strike. After a failure the current value becomes the baseline: the
regression is reported on two consecutive pushes and must then be fixed or accepted.

Usage:
    python tools/ci/bench_compare.py RESULT.json --state STATE.json [--tolerance 0.10]

Without a state file (first run) the result becomes the baseline. The state file is rewritten.
Exit codes: 0 no regression, 1 a benchmark is slower on two consecutive runs, 2 bad input.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

TOLERANCE = 0.10


class BenchError(Exception):
    """A result or state file cannot be read."""


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise BenchError("{}: {}".format(path, e)) from e


def medians(result: Any) -> Dict[str, float]:
    """Median elapsed time per benchmark name of a nanobench JSON result."""
    if not isinstance(result, dict) or not isinstance(result.get("results"), list):
        raise BenchError("not a nanobench JSON result")
    out = {}
    for r in result["results"]:
        name, median = r.get("name"), r.get("median(elapsed)")
        if not isinstance(name, str) or not isinstance(median, (int, float)):
            raise BenchError("a result needs a name and median(elapsed)")
        out[name] = float(median)
    return out


def compare(current: Dict[str, float], state: Optional[Dict[str, Any]],
            tolerance: float) -> Tuple[Dict[str, Any], List[str], List[str]]:
    """New state, the benchmarks with a strike now, and those that fail (two strikes)."""
    if state is None:
        return {"baseline": dict(current), "strikes": []}, [], []
    baseline = state.get("baseline", {})
    previous = set(state.get("strikes", []))
    new_baseline = dict(baseline)
    strikes, failures = [], []
    for name, value in sorted(current.items()):
        base = baseline.get(name)
        if base is not None and value > base * (1.0 + tolerance):
            strikes.append(name)
            if name in previous:
                failures.append(name)
                new_baseline[name] = value
        else:
            new_baseline[name] = value
    return {"baseline": new_baseline, "strikes": strikes}, strikes, failures


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("result", type=Path, help="wg_bench --json output")
    parser.add_argument("--state", type=Path, required=True, help="baseline state (rewritten)")
    parser.add_argument("--tolerance", type=float, default=TOLERANCE,
                        help="relative slowdown that counts as a strike (default 0.10)")
    args = parser.parse_args(argv)
    try:
        current = medians(read_json(args.result))
        state = read_json(args.state) if args.state.is_file() else None
    except BenchError as e:
        print("error: {}".format(e), file=sys.stderr)
        return 2

    new_state, strikes, failures = compare(current, state, args.tolerance)
    args.state.write_text(json.dumps(new_state, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
    if state is None:
        print("no baseline yet: {} benchmarks recorded".format(len(current)))
        return 0
    baseline = state.get("baseline", {})
    for name in sorted(current):
        base = baseline.get(name)
        change = "new" if base is None else "{:+.1f} %".format(100.0 * (current[name] / base - 1.0))
        mark = " FAIL" if name in failures else (" strike" if name in strikes else "")
        print("{:<48} {:>14.6g} s  {}{}".format(name, current[name], change, mark))
    if failures:
        print("{} benchmark(s) more than {:.0f} % slower on two consecutive runs: {}".format(
            len(failures), 100.0 * args.tolerance, ", ".join(failures)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
