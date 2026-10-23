#!/usr/bin/env python3
"""Copy the --help output of wgcli into docs/cli.md (A10).

docs/cli.md holds every help text between markers:

    <!-- BEGIN wgcli help -->             (general help, "wgcli help")
    <!-- BEGIN wgcli <command> --help -->  ("wgcli <command> --help")
    ```text
    ...
    ```
    <!-- END -->

The unit test wgcli.docs (tests/unit/wgcli/test_cli_doc.cpp) fails when a block differs from
the built tool, so ci.yml catches a stale reference. This script rewrites the blocks.

Usage:
    python tools/wgcli/sync_cli_doc.py --wgcli build/debug/tools/wgcli/wgcli
    python tools/wgcli/sync_cli_doc.py --wgcli <exe> --check    # exit 1 if out of date

Standard library only.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

DOC = Path(__file__).resolve().parents[2] / "docs" / "cli.md"
BLOCK = re.compile(r"(<!-- BEGIN wgcli (\S+)(?: --help)? -->\n)(.*?)(<!-- END -->)", re.S)


def help_text(wgcli: str, name: str) -> str:
    args = [wgcli, "help"] if name == "help" else [wgcli, name, "--help"]
    result = subprocess.run(args, stdout=subprocess.PIPE, universal_newlines=True, check=False)
    if result.returncode != 0:
        raise SystemExit("{} exited with {}".format(" ".join(args), result.returncode))
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wgcli", required=True, help="path to the built wgcli executable")
    parser.add_argument("--check", action="store_true", help="only compare, exit 1 if stale")
    args = parser.parse_args()

    with DOC.open(encoding="utf-8") as f:  # universal newlines: CRLF checkouts read as LF
        text = f.read()

    def replace(match: "re.Match[str]") -> str:
        body = "```text\n" + help_text(args.wgcli, match.group(2)) + "```\n"
        return match.group(1) + body + match.group(4)

    updated = BLOCK.sub(replace, text)
    if updated == text:
        print("docs/cli.md is up to date")
        return 0
    if args.check:
        print("docs/cli.md is out of date; run without --check to update it", file=sys.stderr)
        return 1
    with DOC.open("w", encoding="utf-8", newline="\n") as f:
        f.write(updated)
    print("updated docs/cli.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
