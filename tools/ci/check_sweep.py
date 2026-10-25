#!/usr/bin/env python3
"""Check a `wgcli sweep` run against the A23 acceptance criterion (ADR 0021).

Every seed of the metrics CSV must either succeed or fail with a complete report: the
--keep-failures file seed_<s>.json exists, its status is failed_precondition and every
rejection names the stage, the check and finite measured and required values. Prints the
attempts and the wall time per attempt (wall_ms / attempts) as percentiles; with --budget-ms,
also fails if a seed's wall time per attempt exceeds the budget (R6, measured on the reference
machine, never on shared CI runners).

Usage:
    python tools/ci/check_sweep.py METRICS.csv --failures DIR [--budget-ms MS]

Exit codes: 0 the criterion holds, 1 it does not, 2 a file is missing or malformed.
"""

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Optional


class SweepError(Exception):
    """A metrics or failure file cannot be read."""


def read_rows(path: Path) -> List[Dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
    except OSError as e:
        raise SweepError("{}: {}".format(path, e)) from e
    for column in ("seed", "status", "attempts", "wall_ms"):
        if rows and column not in rows[0]:
            raise SweepError("{}: no column {}".format(path, column))
    return rows


def report_problems(path: Path) -> List[str]:
    """What is missing from the failure report of one seed; empty if it is complete."""
    if not path.is_file():
        return ["no failure report {}".format(path.name)]
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise SweepError("{}: {}".format(path, e)) from e
    problems = []
    if doc.get("status") != "failed_precondition":
        problems.append("status {!r} is not failed_precondition".format(doc.get("status")))
    rejections = doc.get("rejections")
    if not isinstance(rejections, list) or not rejections:
        return problems + ["no rejections"]
    for k, r in enumerate(rejections):
        for key in ("stage", "check_id"):
            if not isinstance(r.get(key), str) or not r[key]:
                problems.append("rejection {}: no {}".format(k, key))
        for key in ("measured", "required"):
            v = r.get(key)
            if not isinstance(v, (int, float)) or not math.isfinite(v):
                problems.append("rejection {}: {} is not a finite number".format(k, key))
    return problems


def percentile(values: List[float], q: float) -> float:
    """Nearest-rank percentile q in [0, 100] of a non-empty list."""
    ordered = sorted(values)
    rank = max(1, math.ceil(q / 100.0 * len(ordered)))
    return ordered[rank - 1]


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("metrics", type=Path, help="CSV of wgcli sweep --metrics")
    parser.add_argument("--failures", type=Path, required=True,
                        help="directory of wgcli sweep --keep-failures")
    parser.add_argument("--budget-ms", type=float,
                        help="fail if a seed's wall time per attempt exceeds this")
    args = parser.parse_args(argv)

    try:
        rows = read_rows(args.metrics)
        if not rows:
            raise SweepError("{}: no seeds".format(args.metrics))
        errors = []
        per_attempt = []
        attempts = []
        failed = 0
        for row in rows:
            seed = row["seed"]
            n = int(row["attempts"])
            attempts.append(n)
            per_attempt.append(float(row["wall_ms"]) / max(1, n))
            if row["status"] == "ok":
                continue
            failed += 1
            if row["status"] != "failed":
                errors.append("seed {}: unknown status {!r}".format(seed, row["status"]))
                continue
            for p in report_problems(args.failures / "seed_{}.json".format(seed)):
                errors.append("seed {}: {}".format(seed, p))
        if args.budget_ms is not None:
            for row, t in zip(rows, per_attempt):
                if t > args.budget_ms:
                    errors.append("seed {}: {:.0f} ms per attempt exceeds the budget of {:.0f} ms"
                                  .format(row["seed"], t, args.budget_ms))
    except (SweepError, ValueError) as e:
        print("error: {}".format(e), file=sys.stderr)
        return 2

    print("seeds: {}, ok: {}, failed with a report: {}".format(len(rows), len(rows) - failed,
                                                               failed))
    print("attempts: mean {:.2f}, max {}".format(sum(attempts) / len(attempts), max(attempts)))
    print("wall time per attempt [ms]: p50 {:.0f}, p90 {:.0f}, p95 {:.0f}, max {:.0f}".format(
        percentile(per_attempt, 50), percentile(per_attempt, 90), percentile(per_attempt, 95),
        max(per_attempt)))
    if errors:
        print("{} problem(s):".format(len(errors)))
        for e in errors:
            print("  " + e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
