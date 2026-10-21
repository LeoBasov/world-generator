"""Tests for tools/data/validate_data.py (plan A07).

Run from the repository root: python -m unittest discover -s tools/data/tests -v
"""

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import validate_data as vd  # noqa: E402

try:
    import jsonschema  # noqa: F401
    HAVE_JSONSCHEMA = True
except ImportError:
    HAVE_JSONSCHEMA = False


def run(*args):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = vd.main([str(a) for a in args])
    return rc, out.getvalue() + err.getvalue()


@unittest.skipUnless(HAVE_JSONSCHEMA, "needs jsonschema")
class ShippedData(unittest.TestCase):
    def test_every_shipped_data_file_is_valid(self):
        rc, text = run()
        self.assertEqual(rc, 0, text)

    def test_every_data_file_is_found(self):
        names = {p.name for p in vd.data_files(vd.DATA_DIR)}
        for expected in ("df_default.json", "biome_table.json", "climate_classes.json",
                         "salinity_classes.json", "rock_types.json", "soils.json",
                         "minerals.json"):
            self.assertIn(expected, names)


@unittest.skipUnless(HAVE_JSONSCHEMA, "needs jsonschema")
class BrokenData(unittest.TestCase):
    """Data files written next to the real ones, so their relative $schema resolves."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(dir=str(vd.DATA_DIR))
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, document):
        path = self.tmp / "broken.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def test_out_of_range_parameter_is_reported_with_its_path(self):
        rc, text = run(self.write({"$schema": "../../schema/params.schema.json",
                                   "schema_version": 1, "grid": {"size": 999}}))
        self.assertEqual(rc, 1)
        self.assertIn("$.grid.size", text)

    def test_unknown_parameter(self):
        rc, text = run(self.write({"$schema": "../../schema/params.schema.json",
                                   "schema_version": 1, "erosion": {"FOO": 1}}))
        self.assertEqual(rc, 1)
        self.assertIn("FOO", text)

    def test_missing_schema_reference(self):
        rc, text = run(self.write({"schema_version": 1}))
        self.assertEqual(rc, 1)
        self.assertIn("$schema", text)

    def test_schema_outside_schema_dir(self):
        rc, text = run(self.write({"$schema": "../../data/presets/df_default.json"}))
        self.assertEqual(rc, 1)
        self.assertIn("not a schema", text)

    def test_mineral_with_two_hosts(self):
        rc, text = run(self.write({
            "$schema": "../../schema/minerals.schema.json", "schema_version": 1,
            "minerals": [{"name": "x", "kind": "ore", "confidence": "observed",
                          "environments": [{"host_rock": "granite", "host_class": "metamorphic",
                                            "inclusion": "vein", "frequency": 5}]}]}))
        self.assertEqual(rc, 1)


class WithoutJsonschema(unittest.TestCase):
    @unittest.skipIf(HAVE_JSONSCHEMA, "jsonschema is installed")
    def test_reports_the_missing_package(self):
        rc, text = run()
        self.assertEqual(rc, 2)
        self.assertIn("jsonschema", text)


if __name__ == "__main__":
    unittest.main()
