#!/usr/bin/env python3
"""Generate include/wg/generated/field_ids.hpp from schema/field_registry.json.

The registry is validated against schema/field_registry.schema.json and a few rules that
JSON Schema cannot express. The output is deterministic, so CI can regenerate it and fail
on any difference to the committed header (plan section 8).

Usage:
    python tools/codegen/gen_field_ids.py           # regenerate the header
    python tools/codegen/gen_field_ids.py --check   # exit 1 if the header is out of date

Exit codes: 0 success, 1 header out of date (--check), 2 invalid registry or I/O error.
"""

import argparse
import difflib
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REGISTRY = REPO_ROOT / "schema" / "field_registry.json"
DEFAULT_SCHEMA = REPO_ROOT / "schema" / "field_registry.schema.json"
DEFAULT_OUT = REPO_ROOT / "include" / "wg" / "generated" / "field_ids.hpp"

# dtype -> (C++ value type, inclusive integer limits or None for floating point)
DTYPES = {
    "u8": ("std::uint8_t", (0, 255)),
    "u16": ("std::uint16_t", (0, 65535)),
    "i32": ("std::int32_t", (-(2**31), 2**31 - 1)),
    "f32": ("float", None),
}


class RegistryError(Exception):
    """The registry violates its schema or a generator rule."""


def load_json(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise RegistryError("{}: {}".format(path, e)) from e


def validate_schema(registry: Any, schema: Any) -> None:
    """Validate against the JSON Schema; reports every violation with its JSON path."""
    import jsonschema  # imported here so --help works without the package

    validator_cls = jsonschema.validators.validator_for(schema)
    validator_cls.check_schema(schema)
    errors = sorted(validator_cls(schema).iter_errors(registry), key=lambda e: list(e.path))
    if errors:
        lines = ["{}: {}".format(e.json_path, e.message) for e in errors]
        raise RegistryError("schema violation:\n  " + "\n  ".join(lines))


def _check_ascii(value: Any, where: str) -> None:
    # Non-ASCII literals break MSVC builds without /utf-8 (C4819 under /WX).
    if isinstance(value, str):
        if not value.isascii():
            raise RegistryError("{}: non-ASCII text {!r}".format(where, value))
    elif isinstance(value, list):
        for i, v in enumerate(value):
            _check_ascii(v, "{}[{}]".format(where, i))
    elif isinstance(value, dict):
        for k, v in value.items():
            _check_ascii(v, "{}.{}".format(where, k))


def check_rules(registry: Dict[str, Any]) -> None:
    """Rules beyond the JSON Schema: dense append-only ids, unique names, consistent ranges."""
    names = set()
    for i, field in enumerate(registry["fields"]):
        where = "$.fields[{}]".format(i)
        _check_ascii(field, where)
        if field["id"] != i:
            raise RegistryError(
                "{}.id: is {}, expected {} (ids equal the array position)".format(where, field["id"], i))
        if field["name"] in names:
            raise RegistryError("{}.name: duplicate name {!r}".format(where, field["name"]))
        names.add(field["name"])

        limits = DTYPES[field["dtype"]][1]
        labels = field.get("enum")
        if labels is not None:
            if limits is None:
                raise RegistryError("{}.enum: enum labels need an integer dtype".format(where))
            if len(labels) - 1 > limits[1]:
                raise RegistryError("{}.enum: {} labels do not fit {}".format(
                    where, len(labels), field["dtype"]))

        rng = field.get("range", {})
        lo, hi = rng.get("min"), rng.get("max")
        if lo is not None and hi is not None and lo > hi:
            raise RegistryError("{}.range: min {} > max {}".format(where, lo, hi))
        if limits is not None:
            for key in ("min", "max"):
                v = rng.get(key)
                if v is not None and not (limits[0] <= v <= limits[1]):
                    raise RegistryError("{}.range.{}: {} outside {}".format(where, key, v, field["dtype"]))
        for v in rng.get("values", []):
            if (lo is not None and v < lo) or (hi is not None and v > hi):
                raise RegistryError("{}.range.values: {} outside [min, max]".format(where, v))
            if limits is not None and (v != int(v) or not limits[0] <= v <= limits[1]):
                raise RegistryError("{}.range.values: {} is not a {} value".format(where, v, field["dtype"]))


def load_registry(registry_path: Path, schema_path: Path) -> Dict[str, Any]:
    registry = load_json(registry_path)
    validate_schema(registry, load_json(schema_path))
    check_rules(registry)
    return registry


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------


def _str(s: str) -> str:
    return json.dumps(s)  # ASCII-only by check_rules, so JSON escaping is valid C++


def _num(v: float) -> str:
    return repr(float(v))


def _camel(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_"))


def _effective_range(field: Dict[str, Any]) -> Dict[str, Any]:
    rng = dict(field.get("range", {}))
    labels = field.get("enum")
    if labels is not None:
        rng.setdefault("min", 0)
        rng.setdefault("max", len(labels) - 1)
    return rng


def render(registry: Dict[str, Any]) -> str:
    fields: List[Dict[str, Any]] = registry["fields"]
    out: List[str] = []
    w = out.append

    w("// Generated by tools/codegen/gen_field_ids.py from schema/field_registry.json.")
    w("// Do not edit. Regenerate with: python tools/codegen/gen_field_ids.py")
    w("// clang-format off")
    w("#pragma once")
    w("")
    w("#include <array>")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("#include <optional>")
    w("#include <span>")
    w("#include <string_view>")
    w("")
    w("namespace wg::core {")
    w("")
    w("/// Version of the field registry schema the header was generated from.")
    w("inline constexpr int kFieldRegistrySchemaVersion = {};".format(registry["schema_version"]))
    w("")
    w("/// Raster fields exchanged between stages (plan section 4.1). Values are append-only.")
    w("/// The underlying type leaves room for appended fields, so it is fixed at 16 bits.")
    w("enum class FieldId : std::uint16_t {  // NOLINT(performance-enum-size)")
    for f in fields:
        w("  {} = {},".format(f["name"], f["id"]))
    w("};")
    w("")
    w("/// Number of registered fields.")
    w("inline constexpr std::size_t kFieldCount = {};".format(len(fields)))
    w("")
    w("/// Storage type of a field.")
    w("enum class DType : std::uint8_t { u8, u16, i32, f32 };")
    w("")
    w("/// C++ value type of a dtype.")
    w("template <DType D>")
    w("struct dtype_traits;")
    for dtype, (cpp, _) in DTYPES.items():
        w("template <>")
        w("struct dtype_traits<DType::{}> {{".format(dtype))
        w("  using type = {};".format(cpp))
        w("};")
    w("")
    w("/// Valid values of a field. Bounds that depend on the grid or parameters are in note.")
    w("struct FieldRange {")
    w("  bool has_min;")
    w("  double min;")
    w("  bool min_exclusive;")
    w("  bool has_max;")
    w("  double max;")
    w("  std::span<const double> values;  ///< if non-empty, the only allowed values")
    w("  std::string_view note;")
    w("};")
    w("")
    w("/// Registry entry of one field.")
    w("struct FieldInfo {")
    w("  FieldId id;")
    w("  std::string_view name;")
    w("  DType dtype;")
    w("  std::string_view unit;         ///< unit symbol of the stored value, empty if none")
    w("  std::string_view scale;        ///< meaning of the stored value")
    w("  std::string_view si_mapping;   ///< mapping to SI units, empty if none")
    w("  std::string_view description;")
    w("  FieldRange range;")
    w("};")
    w("")

    enum_fields = [f for f in fields if "enum" in f]
    if enum_fields:
        w("// Labels of enumerated fields, in value order.")
        for f in enum_fields:
            w("enum class {} : {} {{ {} }};".format(
                _camel(f["name"]), DTYPES[f["dtype"]][0], ", ".join(f["enum"])))
        w("")

    value_fields = [f for f in fields if "values" in f.get("range", {})]
    if value_fields:
        w("namespace detail {")
        for f in value_fields:
            vals = f["range"]["values"]
            w("inline constexpr std::array<double, {}> k{}Values = {{{}}};".format(
                len(vals), _camel(f["name"]), ", ".join(_num(v) for v in vals)))
        w("}  // namespace detail")
        w("")

    w("/// All fields, indexed by FieldId.")
    w("inline constexpr std::array<FieldInfo, kFieldCount> kFields = {{")
    for f in fields:
        rng = _effective_range(f)
        values = ("std::span<const double>(detail::k{}Values)".format(_camel(f["name"]))
                  if "values" in rng else "{}")
        w("  FieldInfo{")
        w("    .id = FieldId::{},".format(f["name"]))
        w("    .name = {},".format(_str(f["name"])))
        w("    .dtype = DType::{},".format(f["dtype"]))
        w("    .unit = {},".format(_str(f["unit"])))
        w("    .scale = {},".format(_str(f["scale"])))
        w("    .si_mapping = {},".format(_str(f["si_mapping"])))
        w("    .description = {},".format(_str(f["description"])))
        w("    .range = FieldRange{{.has_min = {}, .min = {}, .min_exclusive = {}, "
          ".has_max = {}, .max = {}, .values = {}, .note = {}}},".format(
              "true" if "min" in rng else "false", _num(rng.get("min", 0)),
              "true" if rng.get("min_exclusive", False) else "false",
              "true" if "max" in rng else "false", _num(rng.get("max", 0)),
              values, _str(rng.get("note", ""))))
        w("  },")
    w("}};")
    w("")
    w("/// Registry entry of a field.")
    w("constexpr const FieldInfo& field_info(FieldId id) noexcept {")
    w("  return kFields[static_cast<std::size_t>(id)];")
    w("}")
    w("")
    w("/// Field with the given name, or nullopt if no field has it.")
    w("constexpr std::optional<FieldId> field_id_from_name(std::string_view name) noexcept {")
    w("  for (const FieldInfo& f : kFields) {")
    w("    if (f.name == name) {")
    w("      return f.id;")
    w("    }")
    w("  }")
    w("  return std::nullopt;")
    w("}")
    w("")
    w("/// C++ value type of a field, e.g. field_t<FieldId::elevation> is float.")
    w("template <FieldId F>")
    w("using field_t = typename dtype_traits<field_info(F).dtype>::type;")
    w("")
    w("}  // namespace wg::core")
    w("// clang-format on")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true",
                        help="compare with --out instead of writing it; exit 1 on a difference")
    args = parser.parse_args(argv)

    try:
        text = render(load_registry(args.registry, args.schema))
    except RegistryError as e:
        print("error: {}: {}".format(args.registry, e), file=sys.stderr)
        return 2

    if args.check:
        try:
            current = args.out.read_text(encoding="utf-8")
        except OSError:
            current = ""
        if current != text:
            diff = difflib.unified_diff(current.splitlines(True), text.splitlines(True),
                                        str(args.out), "generated")
            sys.stderr.writelines(diff)
            print("error: {} is out of date; run tools/codegen/gen_field_ids.py".format(args.out),
                  file=sys.stderr)
            return 1
        return 0

    try:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    except OSError as e:
        print("error: {}".format(e), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
