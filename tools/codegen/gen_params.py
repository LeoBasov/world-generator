#!/usr/bin/env python3
"""Generate include/wg/generated/gen_params.hpp from schema/params.schema.json.

The parameter schema is the single source of truth of the parameter model (A07): every leaf
has a type, a default, inclusive bounds and a description. The generator emits the nested
GenParams structs with the defaults as member initialisers and the descriptor table that the
loader, the validator and the hash walk. The output is deterministic, so CI can regenerate it
and fail on any difference to the committed header (plan section 8).

Generation needs only the standard library. If the jsonschema package is installed (CI), the
schema is also checked against the JSON Schema meta-schema.

Usage:
    python tools/codegen/gen_params.py           # regenerate the header
    python tools/codegen/gen_params.py --check   # exit 1 if the header is out of date

Exit codes: 0 success, 1 header out of date (--check), 2 invalid schema or I/O error.
"""

import argparse
import difflib
import json
import keyword
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCHEMA = REPO_ROOT / "schema" / "params.schema.json"
DEFAULT_OUT = REPO_ROOT / "include" / "wg" / "generated" / "gen_params.hpp"

INT32_MIN, INT32_MAX = -(2**31), 2**31 - 1
IDENT = re.compile(r"^[a-z_][a-z0-9_]*$")
CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
    "case", "catch", "char", "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
    "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires",
    "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
    "xor", "xor_eq",
}
CPP_SCALAR = {"real": "double", "integer": "std::int32_t", "boolean": "bool"}


class SchemaError(Exception):
    """The parameter schema violates a generator rule."""


def load_json(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise SchemaError("{}: {}".format(path, e)) from e


def check_meta_schema(schema: Any) -> None:
    """Checks the schema against its meta-schema if jsonschema is installed."""
    try:
        import jsonschema  # optional: CI installs it, generation works without it
    except ImportError:
        return
    try:
        jsonschema.validators.validator_for(schema).check_schema(schema)
    except jsonschema.exceptions.SchemaError as e:
        raise SchemaError("not a valid JSON Schema: {}".format(e.message)) from e


def _check_ascii(value: Any, where: str) -> None:
    # Non-ASCII literals break MSVC builds without /utf-8 (C4819 under /WX).
    if isinstance(value, str):
        if not value.isascii():
            raise SchemaError("{}: non-ASCII text {!r}".format(where, value))
    elif isinstance(value, list):
        for i, v in enumerate(value):
            _check_ascii(v, "{}[{}]".format(where, i))
    elif isinstance(value, dict):
        for k, v in value.items():
            _check_ascii(k, where)
            _check_ascii(v, "{}.{}".format(where, k))


def _camel(name: str) -> str:
    return "".join(part.capitalize() for part in name.lower().split("_"))


def _cpp_name(key: str, where: str) -> str:
    name = key.lower()
    if not IDENT.match(name) or name in CPP_KEYWORDS or keyword.iskeyword(name):
        raise SchemaError("{}: key {!r} does not map to a C++ identifier".format(where, key))
    return name


def _is_number(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


class Leaf:
    """One parameter: a scalar or a fixed-length array of scalars."""

    def __init__(self, path: str, member: str, kind: str, count: int, minimum: float,
                 maximum: float, defaults: List[float], labels: List[str], enum_type: str,
                 df_name: str, unit: str, report: str, description: str) -> None:
        self.path = path
        self.member = member
        self.kind = kind
        self.count = count
        self.minimum = minimum
        self.maximum = maximum
        self.defaults = defaults
        self.labels = labels
        self.enum_type = enum_type
        self.df_name = df_name
        self.unit = unit
        self.report = report
        self.description = description


class Member:
    def __init__(self, name: str, cpp_type: str, init: str, doc: str) -> None:
        self.name = name
        self.cpp_type = cpp_type
        self.init = init
        self.doc = doc


class Model:
    def __init__(self, schema_version: int) -> None:
        self.schema_version = schema_version
        self.structs: List[Tuple[str, str, List[Member]]] = []  # (name, doc, members)
        self.enums: List[Tuple[str, List[str]]] = []
        self.leaves: List[Leaf] = []


class Builder:
    def __init__(self, schema: Dict[str, Any]) -> None:
        self.schema = schema
        self.defs = schema.get("$defs", {})
        self.struct_names: Dict[str, str] = {}  # struct name -> origin (def or path)
        self.df_stack: List[str] = []  # x-df-name of the enclosing objects
        self.model = Model(schema["properties"]["schema_version"]["const"])

    def resolve(self, node: Dict[str, Any], where: str) -> Tuple[Dict[str, Any], Optional[str]]:
        """Merges a $ref target with the sibling keywords; returns (node, def name)."""
        ref = node.get("$ref")
        if ref is None:
            return node, None
        prefix = "#/$defs/"
        if not isinstance(ref, str) or not ref.startswith(prefix) or ref[len(prefix):] not in self.defs:
            raise SchemaError("{}: unsupported $ref {!r}".format(where, ref))
        name = ref[len(prefix):]
        merged = dict(self.defs[name])
        merged.update({k: v for k, v in node.items() if k != "$ref"})
        return merged, name

    def build(self) -> Model:
        root = self.schema
        if root.get("type") != "object" or root.get("additionalProperties") is not False:
            raise SchemaError("$: must be an object with additionalProperties false")
        doc_keys = root.get("x-document-keys")
        if not isinstance(doc_keys, list) or not all(k in root["properties"] for k in doc_keys):
            raise SchemaError("$.x-document-keys: must list properties of the root")
        if "schema_version" not in doc_keys:
            raise SchemaError("$.x-document-keys: must contain schema_version")
        props = {k: v for k, v in root["properties"].items() if k not in doc_keys}
        self.struct("GenParams", "Generation parameters (A07); defaults from the schema.",
                    props, "$", "", {}, "$")
        return self.model

    def struct(self, name: str, doc: str, props: Dict[str, Any], path: str, access: str,
               overrides: Dict[str, Any], origin: str) -> None:
        """Emits a struct once and records the leaves below it under path."""
        emit = name not in self.struct_names
        if not emit and self.struct_names[name] != origin:
            raise SchemaError("{}: struct name {} is also used by {}".format(
                path, name, self.struct_names[name]))
        self.struct_names[name] = origin
        members: List[Member] = []
        seen = set()
        for key, raw in props.items():
            where = "{}.{}".format(path, key)
            member = _cpp_name(key, where)
            if member in seen:
                raise SchemaError("{}: duplicate C++ member {}".format(where, member))
            seen.add(member)
            node, def_name = self.resolve(raw, where)
            sub_access = "{}.{}".format(access, member) if access else member
            override = overrides.get(key) if isinstance(overrides, dict) else None
            members.append(self.member(key, node, def_name, where, sub_access, override))
        if emit:
            self.model.structs.append((name, doc, members))

    def member(self, key: str, node: Dict[str, Any], def_name: Optional[str], path: str,
               access: str, override: Any) -> Member:
        doc = node.get("description", "")
        if node.get("type") == "object":
            if node.get("additionalProperties") is not False:
                raise SchemaError("{}: objects need additionalProperties false".format(path))
            if def_name is not None:
                name, origin = _camel(def_name) + "Params", "#/$defs/" + def_name
            else:
                name, origin = _camel(path[2:].replace(".", "_")) + "Params", path
            instance = node.get("default", {})
            if not isinstance(instance, dict):
                raise SchemaError("{}.default: must be an object".format(path))
            merged = self.merge_overrides(instance, override)
            props = node.get("properties", {})
            for k in merged:
                if k not in props:
                    raise SchemaError("{}.default: unknown key {!r}".format(path, k))
            df = node.get("x-df-name")
            if df:
                self.df_stack.append(df)
            self.struct(name, doc, props, path, access, merged, origin)
            if df:
                self.df_stack.pop()
            # The member initialiser holds the defaults of this occurrence in its struct; an
            # override from an enclosing default object reaches only the descriptors.
            init = self.render_object(node, instance, path) if instance else "{}"
            return Member(_cpp_name(key, path), name, init, doc)
        leaf = self.leaf(key, node, path, access, override)
        self.model.leaves.append(leaf)
        if leaf.kind == "enumeration":
            cpp_type = leaf.enum_type
        else:
            cpp_type = CPP_SCALAR[leaf.kind]
        if leaf.count:
            cpp_type = "std::array<{}, {}>".format(cpp_type, leaf.count)
        unit = " [{}]".format(leaf.unit) if leaf.unit else ""
        init = self.render_value(node, node["default"], path)
        return Member(_cpp_name(key, path), cpp_type, init, doc + unit)

    @staticmethod
    def merge_overrides(instance: Dict[str, Any], override: Any) -> Dict[str, Any]:
        merged = dict(instance)
        if isinstance(override, dict):
            merged.update(override)
        return merged

    def leaf(self, key: str, node: Dict[str, Any], path: str, access: str,
             override: Any) -> Leaf:
        t = node.get("type")
        count = 0
        item = node
        if t == "array":
            count = node.get("minItems")
            if not isinstance(count, int) or count < 1 or node.get("maxItems") != count:
                raise SchemaError("{}: arrays need minItems == maxItems >= 1".format(path))
            item = node.get("items", {})
            t = item.get("type")
            if t not in ("number", "integer", "boolean"):
                raise SchemaError("{}.items: unsupported item type {!r}".format(path, t))
        if "description" not in node:
            raise SchemaError("{}: missing description".format(path))
        default = override if override is not None else node.get("default")
        if default is None:
            raise SchemaError("{}: missing default".format(path))
        defaults = default if count else [default]
        if not isinstance(defaults, list) or len(defaults) != max(count, 1):
            raise SchemaError("{}.default: expected {} values".format(path, max(count, 1)))

        labels: List[str] = []
        enum_type = ""
        if t == "string":
            labels = node.get("enum", [])
            if not labels or not all(isinstance(s, str) for s in labels):
                raise SchemaError("{}: strings must be enums".format(path))
            for s in labels:
                if not IDENT.match(s) or s in CPP_KEYWORDS:
                    raise SchemaError("{}.enum: {!r} is not a C++ identifier".format(path, s))
            if len(labels) > 256:
                raise SchemaError("{}.enum: more than 256 labels".format(path))
            if defaults[0] not in labels:
                raise SchemaError("{}.default: {!r} is not a label".format(path, defaults[0]))
            kind, lo, hi = "enumeration", 0.0, float(len(labels) - 1)
            enum_type = _camel(path[2:].replace(".", "_"))
            values = [float(labels.index(defaults[0]))]
            self.model.enums.append((enum_type, labels))
        elif t == "boolean":
            if not all(isinstance(v, bool) for v in defaults):
                raise SchemaError("{}.default: expected booleans".format(path))
            kind, lo, hi = "boolean", 0.0, 1.0
            values = [1.0 if v else 0.0 for v in defaults]
        elif t in ("number", "integer"):
            kind = "real" if t == "number" else "integer"
            lo, hi = item.get("minimum"), item.get("maximum")
            if not _is_number(lo) or not _is_number(hi) or lo > hi:
                raise SchemaError("{}: numbers need minimum <= maximum".format(path))
            if kind == "integer":
                if not (isinstance(lo, int) and isinstance(hi, int)
                        and INT32_MIN <= lo and hi <= INT32_MAX):
                    raise SchemaError("{}: integer bounds must be int32".format(path))
                if not all(isinstance(v, int) and not isinstance(v, bool) for v in defaults):
                    raise SchemaError("{}.default: expected integers".format(path))
            if not all(_is_number(v) for v in defaults):
                raise SchemaError("{}.default: expected numbers".format(path))
            for i, v in enumerate(defaults):
                if not lo <= v <= hi:
                    raise SchemaError("{}.default[{}]: {} outside [{}, {}]".format(path, i, v, lo, hi))
            lo, hi = float(lo), float(hi)
            values = [float(v) for v in defaults]
        else:
            raise SchemaError("{}: unsupported type {!r}".format(path, t))

        df = node.get("x-df-name", "")
        parent_df = self.df_stack[-1] if self.df_stack else ""
        if "{FIELD}" in df:
            df = df.replace("{FIELD}", parent_df)
        elif not df and parent_df:
            df = "{}:{}".format(parent_df, key)
        return Leaf(path, access, kind, count, lo, hi, values, labels, enum_type, df,
                    node.get("x-unit", ""), node.get("x-report", ""), node["description"])

    def render_object(self, node: Dict[str, Any], values: Dict[str, Any], path: str) -> str:
        """Full designated initialiser of an object with the given default values."""
        parts = []
        for key, raw in node.get("properties", {}).items():
            child, _ = self.resolve(raw, path + "." + key)
            sub = values.get(key)
            if child.get("type") == "object":
                merged = self.merge_overrides(child.get("default", {}), sub)
                text = self.render_object(child, merged, path + "." + key)
            else:
                text = self.render_value(child, sub if sub is not None else child.get("default"),
                                         path + "." + key)
            parts.append(".{} = {}".format(key.lower(), text))
        return "{" + ", ".join(parts) + "}"

    def render_value(self, node: Dict[str, Any], value: Any, path: str) -> str:
        if node.get("type") == "array":
            item = node.get("items", {})
            return "{" + ", ".join(self.render_scalar(item, v, path) for v in value) + "}"
        return self.render_scalar(node, value, path)

    @staticmethod
    def render_scalar(node: Dict[str, Any], value: Any, path: str) -> str:
        t = node.get("type")
        if t == "boolean":
            return "true" if value else "false"
        if t == "integer":
            return str(int(value))
        if t == "number":
            return _num(value)
        return "{}::{}".format(_camel(path[2:].replace(".", "_")), value)


def _num(v: float) -> str:
    return repr(float(v))


def load_schema(schema_path: Path) -> Model:
    schema = load_json(schema_path)
    if not isinstance(schema, dict):
        raise SchemaError("$: must be an object")
    _check_ascii(schema, "$")
    check_meta_schema(schema)
    try:
        version = schema["properties"]["schema_version"]["const"]
    except (KeyError, TypeError) as e:
        raise SchemaError("$.properties.schema_version: needs a const") from e
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        raise SchemaError("$.properties.schema_version.const: must be a positive integer")
    return Builder(schema).build()


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------


def _str(s: str) -> str:
    return json.dumps(s)  # ASCII-only by _check_ascii, so JSON escaping is valid C++


def render(model: Model) -> str:
    out: List[str] = []
    w = out.append

    w("// Generated by tools/codegen/gen_params.py from schema/params.schema.json.")
    w("// Do not edit. Regenerate with: python tools/codegen/gen_params.py")
    w("// clang-format off")
    w("#pragma once")
    w("")
    w("#include <array>")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("#include <span>")
    w("#include <string_view>")
    w("")
    w("namespace wg::params {")
    w("")
    w("/// Version of the parameter schema the header was generated from.")
    w("inline constexpr int kParamsSchemaVersion = {};".format(model.schema_version))
    w("")
    for name, labels in model.enums:
        w("enum class {} : std::uint8_t {{ {} }};".format(name, ", ".join(labels)))
    w("")
    for name, doc, members in model.structs:
        if doc:
            w("/// {}".format(doc))
        w("struct {} {{".format(name))
        for m in members:
            if m.doc:
                w("  /// {}".format(m.doc))
            w("  {} {} = {};".format(m.cpp_type, m.name, m.init))
        w("")
        w("  friend bool operator==(const {0}&, const {0}&) = default;".format(name))
        w("};")
        w("")

    w("/// Storage of a parameter: double, std::int32_t, bool, or an enum class over std::uint8_t.")
    w("enum class ParamKind : std::uint8_t { real, integer, boolean, enumeration };")
    w("")
    w("/// One parameter of the schema: a scalar or a fixed-length array of scalars.")
    w("struct ParamDescriptor {")
    w("  std::string_view path;                ///< JSON path, e.g. \"$.fields.elevation.min\"")
    w("  ParamKind kind;")
    w("  std::size_t count;                    ///< array length, 0 for a scalar")
    w("  double min;                           ///< inclusive bound of every element")
    w("  double max;                           ///< inclusive bound of every element")
    w("  std::span<const double> defaults;     ///< max(count, 1) values; enum: label index")
    w("  std::span<const std::string_view> labels;  ///< enum labels in value order")
    w("  std::string_view df_name;             ///< DF token, empty if none")
    w("  std::string_view unit;")
    w("  std::string_view report;              ///< report section, e.g. \"R3.4\"")
    w("  std::string_view description;")
    w("  void* (*member)(GenParams&) noexcept;  ///< first element of the parameter in a GenParams")
    w("};")
    w("")

    flat: List[float] = []
    offsets: List[int] = []
    for leaf in model.leaves:
        offsets.append(len(flat))
        flat.extend(leaf.defaults)
    w("namespace detail {")
    w("inline constexpr std::array<double, {}> kParamDefaults = {{{}}};".format(
        len(flat), ", ".join(_num(v) for v in flat)))
    for name, labels in model.enums:
        w("inline constexpr std::array<std::string_view, {}> k{}Labels = {{{}}};".format(
            len(labels), name, ", ".join(_str(s) for s in labels)))
    w("}  // namespace detail")
    w("")
    w("/// Number of parameters.")
    w("inline constexpr std::size_t kParamCount = {};".format(len(model.leaves)))
    w("")
    w("/// Every parameter, in schema order (the order of the hash, A07).")
    w("inline constexpr std::array<ParamDescriptor, kParamCount> kParamDescriptors = {{")
    for leaf, off in zip(model.leaves, offsets):
        labels = ("std::span<const std::string_view>(detail::k{}Labels)".format(leaf.enum_type)
                  if leaf.kind == "enumeration" else "{}")
        target = "p.{}.data()".format(leaf.member) if leaf.count else "&p.{}".format(leaf.member)
        w("  ParamDescriptor{")
        w("    .path = {},".format(_str(leaf.path)))
        w("    .kind = ParamKind::{},".format(leaf.kind))
        w("    .count = {},".format(leaf.count))
        w("    .min = {},".format(_num(leaf.minimum)))
        w("    .max = {},".format(_num(leaf.maximum)))
        w("    .defaults = std::span<const double>(detail::kParamDefaults.data() + {}, {}),".format(
            off, len(leaf.defaults)))
        w("    .labels = {},".format(labels))
        w("    .df_name = {},".format(_str(leaf.df_name)))
        w("    .unit = {},".format(_str(leaf.unit)))
        w("    .report = {},".format(_str(leaf.report)))
        w("    .description = {},".format(_str(leaf.description)))
        w("    .member = [](GenParams& p) noexcept -> void* {{ return {}; }},".format(target))
        w("  },")
    w("}};")
    w("")
    w("/// Descriptor with the given JSON path, or nullptr.")
    w("constexpr const ParamDescriptor* find_param(std::string_view path) noexcept {")
    w("  for (const ParamDescriptor& d : kParamDescriptors) {")
    w("    if (d.path == path) {")
    w("      return &d;")
    w("    }")
    w("  }")
    w("  return nullptr;")
    w("}")
    w("")
    w("}  // namespace wg::params")
    w("// clang-format on")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true",
                        help="compare with --out instead of writing it; exit 1 on a difference")
    args = parser.parse_args(argv)

    try:
        text = render(load_schema(args.schema))
    except SchemaError as e:
        print("error: {}: {}".format(args.schema, e), file=sys.stderr)
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
            print("error: {} is out of date; run tools/codegen/gen_params.py".format(args.out),
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
