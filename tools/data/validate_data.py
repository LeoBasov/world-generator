#!/usr/bin/env python3
"""Validate every JSON file under data/ against the JSON Schema it names (plan 6.8 lint, A07).

Each data file must name its schema with a relative "$schema" path into schema/, e.g.
"../../schema/params.schema.json". Rules that JSON Schema cannot express (coverage of the
biome table, cross-table references, the field registry ranges) are checked by the C++ loaders
and their unit tests.

Usage:
    python tools/data/validate_data.py              # validate data/
    python tools/data/validate_data.py FILE...      # validate the given files

Exit codes: 0 all valid, 1 at least one file is invalid, 2 usage or setup error.
Requires the jsonschema package (tools/codegen/requirements.txt).
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "data"
SCHEMA_DIR = REPO_ROOT / "schema"


class DataError(Exception):
    """A data file cannot be validated or violates its schema."""


def load_json(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise DataError("{}: {}".format(path, e)) from e


def schema_path(data_path: Path, document: Any) -> Path:
    """The schema a data file names; it must lie in schema/."""
    if not isinstance(document, dict) or not isinstance(document.get("$schema"), str):
        raise DataError("{}: missing \"$schema\" naming its schema".format(data_path))
    path = (data_path.parent / document["$schema"]).resolve()
    if path.parent != SCHEMA_DIR.resolve() or not path.name.endswith(".schema.json"):
        raise DataError("{}: $schema {!r} is not a schema in schema/".format(
            data_path, document["$schema"]))
    if not path.is_file():
        raise DataError("{}: schema {} does not exist".format(data_path, path.name))
    return path


def validate_file(data_path: Path) -> List[str]:
    """Violations of one data file, each as "<file>: <json path>: <message>"."""
    import jsonschema  # imported here so --help works without the package

    document = load_json(data_path)
    schema = load_json(schema_path(data_path, document))
    validator_cls = jsonschema.validators.validator_for(schema)
    validator_cls.check_schema(schema)
    errors = sorted(validator_cls(schema).iter_errors(document), key=lambda e: list(e.path))
    return ["{}: {}: {}".format(data_path, e.json_path, e.message) for e in errors]


def data_files(root: Path) -> List[Path]:
    return sorted(p for p in root.rglob("*.json") if p.is_file())


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("files", nargs="*", type=Path, help="data files (default: data/**/*.json)")
    args = parser.parse_args(argv)

    try:
        import jsonschema  # noqa: F401
    except ImportError:
        print("error: the jsonschema package is required (pip install -r "
              "tools/codegen/requirements.txt)", file=sys.stderr)
        return 2

    files = args.files or data_files(DATA_DIR)
    if not files:
        print("error: no data files found", file=sys.stderr)
        return 2
    failed = False
    for path in files:
        try:
            problems = validate_file(path)
        except DataError as e:
            problems = [str(e)]
        for p in problems:
            print("error: {}".format(p), file=sys.stderr)
        failed = failed or bool(problems)
    if not failed:
        print("{} data files valid".format(len(files)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
