#!/usr/bin/env python3
"""Compare two hash files of `wgcli hash --seeds ... --out` (golden and determinism checks, A23).

Reports every difference by case, seed and stage instead of a raw text diff: the status, the
successful attempt, the world hash and the per-stage hashes. The first differing stage of a
seed is the stage whose output changed; later stages differ as a consequence.

Usage:
    python tools/ci/compare_hashes.py EXPECTED ACTUAL [--max-report N]

Exit codes: 0 identical, 1 the files differ, 2 a file is missing or malformed.
"""

import argparse
import json
import sys
from typing import Any, Dict, List, Optional, Tuple

MAX_REPORT = 20


class HashFileError(Exception):
    """A hash file cannot be read or does not have the expected layout."""


def load(path: str) -> Dict[Tuple[str, int], Dict[str, Any]]:
    """Results of a hash file keyed by (case name, seed)."""
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        raise HashFileError("{}: {}".format(path, e)) from e
    if not isinstance(doc, dict) or doc.get("schema_version") != 1 or \
            not isinstance(doc.get("cases"), list):
        raise HashFileError("{}: not a version 1 hash file".format(path))
    results = {}
    for case in doc["cases"]:
        if not isinstance(case, dict) or not isinstance(case.get("name"), str) or \
                not isinstance(case.get("results"), list):
            raise HashFileError("{}: a case needs a name and results".format(path))
        for r in case["results"]:
            if not isinstance(r, dict) or not isinstance(r.get("seed"), int):
                raise HashFileError("{}: case {}: a result needs a seed".format(
                    path, case["name"]))
            results[(case["name"], r["seed"])] = r
    return results


def stage_list(result: Dict[str, Any]) -> List[Tuple[str, str]]:
    return [(s.get("stage", "?"), s.get("world", "?")) for s in result.get("stages", [])]


def differences(expected: Dict[Tuple[str, int], Dict[str, Any]],
                actual: Dict[Tuple[str, int], Dict[str, Any]]) -> List[str]:
    """One line per differing (case, seed), in case and seed order."""
    out = []
    for key in sorted(set(expected) | set(actual)):
        where = "{} seed {}".format(*key)
        if key not in actual:
            out.append("{}: missing from the actual hashes".format(where))
            continue
        if key not in expected:
            out.append("{}: not in the expected hashes".format(where))
            continue
        e, a = expected[key], actual[key]
        for field in ("status", "attempt"):
            if e.get(field) != a.get(field):
                out.append("{}: {} {!r} != expected {!r}".format(where, field, a.get(field),
                                                                 e.get(field)))
                break
        else:
            es, as_ = stage_list(e), stage_list(a)
            first = next((k for k in range(min(len(es), len(as_))) if es[k] != as_[k]), None)
            if first is not None:
                out.append("{}: first difference after stage {} ({} != expected {})".format(
                    where, as_[first][0], as_[first][1], es[first][1]))
            elif len(es) != len(as_):
                out.append("{}: {} stages != expected {}".format(where, len(as_), len(es)))
            elif e.get("world") != a.get("world"):
                out.append("{}: world {} != expected {}".format(where, a.get("world"),
                                                                e.get("world")))
    return out


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("expected")
    parser.add_argument("actual")
    parser.add_argument("--max-report", type=int, default=MAX_REPORT,
                        help="differences to print (default {})".format(MAX_REPORT))
    args = parser.parse_args(argv)
    try:
        diff = differences(load(args.expected), load(args.actual))
    except HashFileError as e:
        print("error: {}".format(e), file=sys.stderr)
        return 2
    if not diff:
        print("hashes identical: {} and {}".format(args.expected, args.actual))
        return 0
    print("{} of {} differ from {}:".format(args.actual, len(diff), args.expected))
    for line in diff[:args.max_report]:
        print("  " + line)
    if len(diff) > args.max_report:
        print("  ... and {} more".format(len(diff) - args.max_report))
    return 1


if __name__ == "__main__":
    sys.exit(main())
