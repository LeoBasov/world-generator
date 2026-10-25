"""Tests for tools/ci/compare_hashes.py, check_sweep.py and bench_compare.py (A23).

Run from the repository root: python -m unittest discover -s tools/ci/tests -v
"""

import copy
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import bench_compare as bc  # noqa: E402
import check_sweep as cs  # noqa: E402
import compare_hashes as ch  # noqa: E402


def run(module, *args):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = module.main([str(a) for a in args])
    return rc, out.getvalue() + err.getvalue()


def hashes(world="0x1", stage2="0x2", attempt=0, status="ok"):
    result = {"seed": 1, "attempt": attempt, "status": status}
    if status == "ok":
        result["world"] = world
        result["stages"] = [{"stage": "c01_allocate", "world": "0x9"},
                            {"stage": "c02_seed", "world": stage2}]
    return {"schema_version": 1,
            "cases": [{"name": "classic_65", "results": [result]}]}


class CompareHashes(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, doc):
        path = Path(self.dir.name) / name
        path.write_text(json.dumps(doc), encoding="utf-8")
        return path

    def test_identical_files_pass(self):
        a = self.write("a.json", hashes())
        b = self.write("b.json", hashes())
        rc, text = run(ch, a, b)
        self.assertEqual(rc, 0, text)

    def test_reports_the_first_differing_stage(self):
        a = self.write("a.json", hashes())
        b = self.write("b.json", hashes(world="0x5", stage2="0x6"))
        rc, text = run(ch, a, b)
        self.assertEqual(rc, 1)
        self.assertIn("classic_65 seed 1: first difference after stage c02_seed", text)

    def test_reports_attempt_status_and_missing_seeds(self):
        base = hashes()
        self.assertEqual(ch.differences(ch.load(self.write("a.json", base)),
                                        ch.load(self.write("b.json", hashes(attempt=2)))),
                         ["classic_65 seed 1: attempt 2 != expected 0"])
        failed = ch.load(self.write("c.json", hashes(status="failed")))
        self.assertIn("status", ch.differences(ch.load(self.write("d.json", base)), failed)[0])
        other = copy.deepcopy(base)
        other["cases"][0]["results"][0]["seed"] = 2
        diff = ch.differences(ch.load(self.write("e.json", base)),
                              ch.load(self.write("f.json", other)))
        self.assertEqual(diff, ["classic_65 seed 1: missing from the actual hashes",
                                "classic_65 seed 2: not in the expected hashes"])

    def test_malformed_file_is_a_usage_error(self):
        a = self.write("a.json", hashes())
        bad = self.write("bad.json", {"cases": 3})
        self.assertEqual(run(ch, a, bad)[0], 2)
        self.assertEqual(run(ch, a, Path(self.dir.name) / "missing.json")[0], 2)


CSV_HEADER = "seed,status,attempts,rejections,last_check,wall_ms\n"


class CheckSweep(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = Path(self.dir.name)
        self.failures = self.root / "failures"
        self.failures.mkdir()

    def csv(self, rows):
        path = self.root / "metrics.csv"
        path.write_text(CSV_HEADER + "".join(r + "\n" for r in rows), encoding="utf-8")
        return path

    def report(self, seed, rejections, status="failed_precondition"):
        doc = {"seed": seed, "status": status, "rejections": rejections}
        (self.failures / "seed_{}.json".format(seed)).write_text(json.dumps(doc),
                                                                  encoding="utf-8")

    def test_all_ok_passes_and_prints_percentiles(self):
        path = self.csv(["1,ok,1,0,,3000", "2,ok,2,1,PEAK_NUMBER_MIN,8000"])
        rc, text = run(cs, path, "--failures", self.failures)
        self.assertEqual(rc, 0, text)
        self.assertIn("p50 3000", text)
        self.assertIn("max 4000", text)

    def test_failure_needs_a_complete_report(self):
        path = self.csv(["1,ok,1,0,,3000", "2,failed,4,4,RIVER_MINS[0],900"])
        rc, text = run(cs, path, "--failures", self.failures)
        self.assertEqual(rc, 1)
        self.assertIn("seed 2: no failure report", text)
        self.report(2, [{"stage": "c06_rivers", "check_id": "RIVER_MINS[0]", "measured": 3,
                         "required": 400}])
        rc, text = run(cs, path, "--failures", self.failures)
        self.assertEqual(rc, 0, text)
        self.report(2, [{"stage": "", "check_id": "RIVER_MINS[0]", "measured": 3}])
        rc, text = run(cs, path, "--failures", self.failures)
        self.assertEqual(rc, 1)
        self.assertIn("rejection 0: no stage", text)
        self.assertIn("rejection 0: required is not a finite number", text)

    def test_budget_is_per_attempt(self):
        path = self.csv(["1,ok,1,0,,12000", "2,ok,3,2,PEAK_NUMBER_MIN,12000"])
        rc, text = run(cs, path, "--failures", self.failures, "--budget-ms", 10000)
        self.assertEqual(rc, 1)
        self.assertIn("seed 1: 12000 ms per attempt", text)
        self.assertNotIn("seed 2:", text)

    def test_percentile_is_nearest_rank(self):
        self.assertEqual(cs.percentile([5.0, 1.0, 3.0, 2.0, 4.0], 50), 3.0)
        self.assertEqual(cs.percentile([1.0, 2.0], 95), 2.0)
        self.assertEqual(cs.percentile([7.0], 0), 7.0)


def nanobench(**medians):
    return {"results": [{"name": k, "median(elapsed)": v} for k, v in medians.items()]}


class BenchCompare(unittest.TestCase):
    def test_first_run_records_the_baseline(self):
        state, strikes, failures = bc.compare({"a": 1.0}, None, 0.1)
        self.assertEqual(state, {"baseline": {"a": 1.0}, "strikes": []})
        self.assertEqual((strikes, failures), ([], []))

    def test_one_strike_keeps_the_baseline_two_fail(self):
        state = {"baseline": {"a": 1.0, "b": 1.0}, "strikes": []}
        state, strikes, failures = bc.compare({"a": 1.2, "b": 0.9}, state, 0.1)
        self.assertEqual((strikes, failures), (["a"], []))
        self.assertEqual(state["baseline"], {"a": 1.0, "b": 0.9})
        state, strikes, failures = bc.compare({"a": 1.15, "b": 0.9}, state, 0.1)
        self.assertEqual((strikes, failures), (["a"], ["a"]))
        # After the failure the slower value is the baseline.
        self.assertEqual(state["baseline"]["a"], 1.15)

    def test_a_recovered_benchmark_clears_its_strike(self):
        state = {"baseline": {"a": 1.0}, "strikes": ["a"]}
        state, strikes, failures = bc.compare({"a": 1.05}, state, 0.1)
        self.assertEqual((strikes, failures), ([], []))
        self.assertEqual(state, {"baseline": {"a": 1.05}, "strikes": []})

    def test_main_rewrites_the_state(self):
        with tempfile.TemporaryDirectory() as d:
            result = Path(d) / "bench.json"
            state = Path(d) / "state.json"
            result.write_text(json.dumps(nanobench(a=1.0)), encoding="utf-8")
            self.assertEqual(run(bc, result, "--state", state)[0], 0)
            self.assertEqual(json.loads(state.read_text())["baseline"], {"a": 1.0})
            result.write_text(json.dumps(nanobench(a=2.0)), encoding="utf-8")
            self.assertEqual(run(bc, result, "--state", state)[0], 0)
            rc, text = run(bc, result, "--state", state)
            self.assertEqual(rc, 1, text)
            self.assertIn("FAIL", text)
            result.write_text("{}", encoding="utf-8")
            self.assertEqual(run(bc, result, "--state", state)[0], 2)


if __name__ == "__main__":
    unittest.main()
