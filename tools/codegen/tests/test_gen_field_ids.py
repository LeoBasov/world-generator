"""Tests for tools/codegen/gen_field_ids.py (plan A02).

Run from the repository root: python -m unittest discover -s tools/codegen/tests -v
"""

import copy
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import gen_field_ids as gen  # noqa: E402


def _valid_registry():
    return gen.load_json(gen.DEFAULT_REGISTRY)


class RegistryFiles(unittest.TestCase):
    """Runs the generator on registries written to a temporary directory."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write_registry(self, registry):
        path = self.tmp / "field_registry.json"
        path.write_text(json.dumps(registry), encoding="utf-8")
        return path

    def run_main(self, *args):
        err = io.StringIO()
        with redirect_stderr(err):
            rc = gen.main([str(a) for a in args])
        return rc, err.getvalue()


class CommittedHeader(RegistryFiles):
    def test_real_registry_is_valid(self):
        registry = gen.load_registry(gen.DEFAULT_REGISTRY, gen.DEFAULT_SCHEMA)
        self.assertGreater(len(registry["fields"]), 0)

    def test_committed_header_is_up_to_date(self):
        rc, err = self.run_main("--check")
        self.assertEqual(rc, 0, err)

    def test_output_is_deterministic(self):
        registry = gen.load_registry(gen.DEFAULT_REGISTRY, gen.DEFAULT_SCHEMA)
        self.assertEqual(gen.render(registry), gen.render(copy.deepcopy(registry)))

    def test_written_file_uses_lf(self):
        out = self.tmp / "field_ids.hpp"
        rc, err = self.run_main("--out", out)
        self.assertEqual(rc, 0, err)
        data = out.read_bytes()
        self.assertNotIn(b"\r\n", data)
        self.assertEqual(data.decode("utf-8"), gen.render(_valid_registry()))

    def test_check_fails_on_stale_header(self):
        out = self.tmp / "field_ids.hpp"
        self.assertEqual(self.run_main("--out", out)[0], 0)
        out.write_bytes(out.read_bytes().replace(b"= 0,", b"= 7,", 1))
        rc, err = self.run_main("--out", out, "--check")
        self.assertEqual(rc, 1)
        self.assertIn("out of date", err)

    def test_check_fails_on_missing_header(self):
        rc, _ = self.run_main("--out", self.tmp / "missing.hpp", "--check")
        self.assertEqual(rc, 1)


class InvalidRegistries(RegistryFiles):
    """Every broken registry is rejected with exit code 2 and nothing is written."""

    def assert_rejected(self, mutate, message):
        registry = _valid_registry()
        mutate(registry)
        path = self.write_registry(registry)
        out = self.tmp / "field_ids.hpp"
        rc, err = self.run_main("--registry", path, "--out", out)
        self.assertEqual(rc, 2, err)
        self.assertIn(message, err)
        self.assertFalse(out.exists())

    def field(self, registry, name):
        return next(f for f in registry["fields"] if f["name"] == name)

    # Schema violations
    def test_unknown_dtype(self):
        self.assert_rejected(lambda r: r["fields"][0].update(dtype="f64"), "schema violation")

    def test_name_longer_than_32_bytes(self):
        self.assert_rejected(lambda r: r["fields"][0].update(name="a" * 33), "schema violation")

    def test_uppercase_name(self):
        self.assert_rejected(lambda r: r["fields"][0].update(name="Elevation"), "schema violation")

    def test_missing_unit(self):
        self.assert_rejected(lambda r: r["fields"][0].pop("unit"), "$.fields[0]")

    def test_extra_property(self):
        self.assert_rejected(lambda r: r["fields"][0].update(colour="red"), "schema violation")

    def test_wrong_schema_version(self):
        self.assert_rejected(lambda r: r.update(schema_version=2), "$.schema_version")

    def test_empty_field_list(self):
        self.assert_rejected(lambda r: r.update(fields=[]), "schema violation")

    def test_min_exclusive_without_min(self):
        self.assert_rejected(lambda r: r["fields"][0].update(range={"min_exclusive": True}),
                             "schema violation")

    # Generator rules
    def test_id_gap(self):
        self.assert_rejected(lambda r: r["fields"][1].update(id=5), "expected 1")

    def test_duplicate_name(self):
        self.assert_rejected(lambda r: r["fields"][1].update(name=r["fields"][0]["name"]),
                             "duplicate name")

    def test_min_greater_than_max(self):
        self.assert_rejected(lambda r: r["fields"][0].update(range={"min": 2, "max": 1}),
                             "min 2 > max 1")

    def test_enum_on_float_field(self):
        self.assert_rejected(lambda r: self.field(r, "elevation").update(enum=["low", "high"]),
                             "integer dtype")

    def test_value_outside_range(self):
        self.assert_rejected(
            lambda r: self.field(r, "salinity")["range"]["values"].append(200), "outside [min, max]")

    def test_bound_outside_dtype(self):
        self.assert_rejected(lambda r: self.field(r, "salinity")["range"].update(max=300),
                             "outside u8")

    def test_non_ascii_text(self):
        self.assert_rejected(lambda r: r["fields"][0].update(description="≥ 0"), "non-ASCII")

    def test_malformed_json(self):
        path = self.tmp / "field_registry.json"
        path.write_text("{ not json", encoding="utf-8")
        rc, _ = self.run_main("--registry", path, "--out", self.tmp / "out.hpp")
        self.assertEqual(rc, 2)


class Rendering(unittest.TestCase):
    def setUp(self):
        self.text = gen.render(_valid_registry())

    def test_enum_fields_get_label_enums(self):
        self.assertIn("enum class Water : std::uint8_t { land, ocean, lake, river };", self.text)
        self.assertIn("enum class Aquifer : std::uint8_t { none, light, heavy };", self.text)

    def test_enum_fields_get_label_range(self):
        start = self.text.index(".id = FieldId::aquifer")
        self.assertIn(".has_max = true, .max = 2.0", self.text[start:start + 800])

    def test_value_sets(self):
        self.assertIn("kSalinityValues = {0.0, 33.0, 66.0, 100.0};", self.text)

    def test_header_is_excluded_from_clang_format(self):
        lines = self.text.splitlines()
        self.assertIn("// clang-format off", lines[:3])
        self.assertEqual(lines[-1], "// clang-format on")


if __name__ == "__main__":
    unittest.main()
