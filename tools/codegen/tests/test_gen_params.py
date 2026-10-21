"""Tests for tools/codegen/gen_params.py (plan A07).

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

import gen_params as gen  # noqa: E402

try:
    import jsonschema  # noqa: F401
    HAVE_JSONSCHEMA = True
except ImportError:
    HAVE_JSONSCHEMA = False


def _schema():
    return gen.load_json(gen.DEFAULT_SCHEMA)


def _minimal(properties):
    """A schema with the document keys and the given parameter properties."""
    props = {"$schema": {"type": "string"}, "schema_version": {"const": 1}}
    props.update(properties)
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "additionalProperties": False,
        "x-document-keys": ["$schema", "schema_version"],
        "properties": props,
    }


def _number(default=1.0, lo=0.0, hi=10.0):
    return {"description": "x", "type": "number", "minimum": lo, "maximum": hi, "default": default}


class SchemaFiles(unittest.TestCase):
    """Runs the generator on schemas written to a temporary directory."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write_schema(self, schema):
        path = self.tmp / "params.schema.json"
        path.write_text(json.dumps(schema), encoding="utf-8")
        return path

    def run_main(self, *args):
        err = io.StringIO()
        with redirect_stderr(err):
            rc = gen.main([str(a) for a in args])
        return rc, err.getvalue()

    def assert_rejected(self, schema, needle):
        rc, err = self.run_main("--schema", self.write_schema(schema), "--out", self.tmp / "o.hpp")
        self.assertEqual(rc, 2, err)
        self.assertIn(needle, err)


class CommittedHeader(SchemaFiles):
    def test_committed_header_is_up_to_date(self):
        rc, err = self.run_main("--check")
        self.assertEqual(rc, 0, err)

    def test_output_is_deterministic(self):
        a = gen.render(gen.load_schema(gen.DEFAULT_SCHEMA))
        b = gen.render(gen.load_schema(gen.DEFAULT_SCHEMA))
        self.assertEqual(a, b)

    def test_check_reports_a_stale_header(self):
        out = self.tmp / "gen_params.hpp"
        out.write_text("// stale\n", encoding="utf-8")
        rc, err = self.run_main("--check", "--out", out)
        self.assertEqual(rc, 1)
        self.assertIn("out of date", err)

    def test_write_then_check(self):
        out = self.tmp / "gen_params.hpp"
        self.assertEqual(self.run_main("--out", out)[0], 0)
        self.assertEqual(self.run_main("--check", "--out", out)[0], 0)


class Model(unittest.TestCase):
    def setUp(self):
        self.model = gen.load_schema(gen.DEFAULT_SCHEMA)

    def test_every_default_lies_in_its_range(self):
        for leaf in self.model.leaves:
            for v in leaf.defaults:
                self.assertTrue(leaf.minimum <= v <= leaf.maximum, leaf.path)

    def test_paths_are_unique(self):
        paths = [leaf.path for leaf in self.model.leaves]
        self.assertEqual(len(paths), len(set(paths)))

    def test_instance_defaults_reach_the_descriptors(self):
        by_path = {leaf.path: leaf for leaf in self.model.leaves}
        self.assertEqual(by_path["$.fields.elevation.max"].defaults, [400.0])
        self.assertEqual(by_path["$.fields.rainfall.max"].defaults, [100.0])
        self.assertEqual(by_path["$.fields.temperature.min"].defaults, [25.0])

    def test_df_names_follow_the_enclosing_object(self):
        by_path = {leaf.path: leaf for leaf in self.model.leaves}
        self.assertEqual(by_path["$.fields.elevation.weights"].df_name, "ELEVATION_FREQUENCY")
        self.assertEqual(by_path["$.fields.savagery.min"].df_name, "SAVAGERY")
        self.assertEqual(by_path["$.checks.REGION_COUNTS.HILLS"].df_name, "REGION_COUNTS:HILLS")
        self.assertEqual(by_path["$.erosion.EROSION_CYCLE_COUNT"].df_name, "EROSION_CYCLE_COUNT")

    def test_shared_struct_is_emitted_once_with_its_own_defaults(self):
        structs = {name: members for name, _, members in self.model.structs}
        self.assertEqual([n for n, _, _ in self.model.structs].count("FieldGenParams"), 1)
        inits = {m.name: m.init for m in structs["FieldGenParams"]}
        self.assertEqual(inits["max"], "100.0")  # the def's default, not elevation's 400
        fields = {m.name: m.init for m in structs["FieldsParams"]}
        self.assertIn(".max = 400.0", fields["elevation"])

    def test_enum_and_document_keys(self):
        self.assertEqual(self.model.enums, [("Poles", ["none", "north", "south", "both"])])
        paths = {leaf.path for leaf in self.model.leaves}
        for key in ("$.seed", "$.profile", "$.schema_version"):
            self.assertNotIn(key, paths)


class Rejection(SchemaFiles):
    def test_valid_minimal_schema(self):
        rc, err = self.run_main("--schema", self.write_schema(_minimal({"a": _number()})),
                                "--out", self.tmp / "o.hpp")
        self.assertEqual(rc, 0, err)
        text = (self.tmp / "o.hpp").read_text(encoding="utf-8")
        self.assertIn("double a = 1.0;", text)
        self.assertIn('.path = "$.a"', text)

    def test_missing_default(self):
        leaf = _number()
        del leaf["default"]
        self.assert_rejected(_minimal({"a": leaf}), "missing default")

    def test_missing_description(self):
        leaf = _number()
        del leaf["description"]
        self.assert_rejected(_minimal({"a": leaf}), "missing description")

    def test_default_out_of_range(self):
        self.assert_rejected(_minimal({"a": _number(default=11.0)}), "outside")

    def test_min_above_max(self):
        self.assert_rejected(_minimal({"a": _number(lo=5.0, hi=1.0)}), "minimum <= maximum")

    def test_integer_bounds_beyond_int32(self):
        leaf = {"description": "x", "type": "integer", "minimum": 0, "maximum": 2**40, "default": 0}
        self.assert_rejected(_minimal({"a": leaf}), "int32")

    def test_integer_with_fractional_default(self):
        leaf = {"description": "x", "type": "integer", "minimum": 0, "maximum": 9, "default": 1.5}
        self.assert_rejected(_minimal({"a": leaf}), "expected integers")

    def test_variable_length_array(self):
        leaf = {"description": "x", "type": "array", "minItems": 1, "maxItems": 3, "default": [1],
                "items": {"type": "number", "minimum": 0, "maximum": 1}}
        self.assert_rejected(_minimal({"a": leaf}), "minItems == maxItems")

    def test_array_default_of_wrong_length(self):
        leaf = {"description": "x", "type": "array", "minItems": 2, "maxItems": 2, "default": [1],
                "items": {"type": "number", "minimum": 0, "maximum": 1}}
        self.assert_rejected(_minimal({"a": leaf}), "expected 2 values")

    def test_plain_string(self):
        leaf = {"description": "x", "type": "string", "default": "a"}
        self.assert_rejected(_minimal({"a": leaf}), "strings must be enums")

    def test_enum_default_not_a_label(self):
        leaf = {"description": "x", "type": "string", "enum": ["a", "b"], "default": "c"}
        self.assert_rejected(_minimal({"a": leaf}), "is not a label")

    def test_key_that_is_a_keyword(self):
        self.assert_rejected(_minimal({"class": _number()}), "C++ identifier")

    def test_object_without_closed_properties(self):
        obj = {"type": "object", "properties": {"a": _number()}}
        self.assert_rejected(_minimal({"o": obj}), "additionalProperties false")

    def test_unknown_ref(self):
        self.assert_rejected(_minimal({"a": {"$ref": "#/$defs/missing"}}), "unsupported $ref")

    def test_unknown_key_in_instance_default(self):
        obj = {"type": "object", "additionalProperties": False, "default": {"b": 1},
               "properties": {"a": _number()}}
        self.assert_rejected(_minimal({"o": obj}), "unknown key")

    def test_non_ascii_text(self):
        leaf = _number()
        leaf["description"] = "°C"
        self.assert_rejected(_minimal({"a": leaf}), "non-ASCII")

    def test_document_keys_must_exist(self):
        schema = _minimal({"a": _number()})
        schema["x-document-keys"] = ["schema_version", "seed"]
        self.assert_rejected(schema, "x-document-keys")

    def test_struct_name_collision(self):
        inner = {"type": "object", "additionalProperties": False, "properties": {"x": _number()}}
        # "a_b" and "a.b" both map to the struct name ABParams.
        schema = _minimal({
            "a_b": copy.deepcopy(inner),
            "a": {"type": "object", "additionalProperties": False,
                  "properties": {"b": copy.deepcopy(inner)}},
        })
        self.assert_rejected(schema, "struct name")

    @unittest.skipUnless(HAVE_JSONSCHEMA, "needs jsonschema")
    def test_invalid_meta_schema(self):
        schema = _minimal({"a": _number()})
        schema["properties"]["a"]["minimum"] = "zero"
        self.assert_rejected(schema, "not a valid JSON Schema")


if __name__ == "__main__":
    unittest.main()
