#!/usr/bin/env python3
"""Review image set of the visual review (plan section 10.3, test level 7; A23).

Generates one world per seed with wgcli, exports the fields the review checklist looks at as
colour-mapped PNGs (elevation with the terrain map, the others with viridis) and writes an
index.html with one row per seed. golden-update.yml runs it for the branch and for main
("after" and "before"); the checklist results go to docs/review/.

Usage:
    python tools/review/review_set.py --wgcli build/release/tools/wgcli/wgcli --out review \\
        [--seeds 1,2,3] [--size 257] [--profile classic] [--params NAME|FILE]

Exit codes: 0 every seed exported, 1 a seed failed to generate or export, 2 usage error.
"""

import argparse
import html
import subprocess
import sys
from pathlib import Path
from typing import List, Optional

# Fields of the review checklist and the colour map each is shown with.
FIELDS = [
    ("elevation", "terrain"),
    ("water", "viridis"),
    ("rainfall", "viridis"),
    ("temperature", "viridis"),
    ("drainage", "viridis"),
    ("vegetation", "viridis"),
    ("salinity", "viridis"),
    ("biome", "viridis"),
    ("region_id", "viridis"),
]


def parse_seeds(text: str) -> List[int]:
    seeds = []
    for part in text.split(","):
        if ".." in part:
            lo, hi = part.split("..", 1)
            seeds.extend(range(int(lo), int(hi) + 1))
        elif part:
            seeds.append(int(part))
    if not seeds:
        raise ValueError("no seeds")
    return seeds


def run(cmd: List[str]) -> bool:
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True)
    if result.returncode != 0:
        print("failed ({}): {}\n{}".format(result.returncode, " ".join(cmd), result.stdout),
              file=sys.stderr)
    return result.returncode == 0


def export_seed(wgcli: str, out: Path, seed: int, args: argparse.Namespace) -> bool:
    seed_dir = out / "seed_{}".format(seed)
    world = out / "seed_{}.wgw".format(seed)
    cmd = [wgcli, "generate", "--profile", args.profile, "--seed", str(seed), "--size",
           str(args.size), "--threads", "0", "--quiet", "--out", str(world)]
    if args.params:
        cmd += ["--params", args.params]
    if not run(cmd):
        return False
    for colormap in sorted({m for _, m in FIELDS}):
        # Elevation keeps the registry range (sea level at the same colour in every world); the
        # other fields span their values, since temperature or biome ids fill a small part of
        # their registry range.
        map_range = "registry" if colormap == "terrain" else "data"
        cmd = [wgcli, "export", str(world), "--format", "png16", "--colormap", colormap,
               "--map-range", map_range, "--out", str(seed_dir)]
        for field in (f for f, m in FIELDS if m == colormap):
            cmd += ["--field", field]
        if not run(cmd):
            return False
    # Keep the colour-mapped images only; the 16-bit greyscale PNGs are for reading back.
    for field, _ in FIELDS:
        grey = seed_dir / "{}.png".format(field)
        if grey.exists():
            grey.unlink()
    if not args.keep_worlds:
        world.unlink()
    return True


def write_index(out: Path, seeds: List[int], ok: List[int], args: argparse.Namespace) -> None:
    title = "Review set: {} N = {}{}".format(args.profile, args.size,
                                             ", " + args.params if args.params else "")
    rows = []
    header = "".join("<th>{}</th>".format(html.escape(f)) for f, _ in FIELDS)
    for seed in seeds:
        if seed not in ok:
            rows.append("<tr><th>{}</th><td colspan='{}'>failed</td></tr>".format(
                seed, len(FIELDS)))
            continue
        cells = "".join(
            "<td><img src='seed_{0}/{1}_{2}.png' alt='{1}' width='192'></td>".format(seed, f, m)
            for f, m in FIELDS)
        rows.append("<tr><th>{}</th>{}</tr>".format(seed, cells))
    page = ("<!doctype html><meta charset='utf-8'><title>{0}</title>"
            "<style>body{{font-family:sans-serif}}img{{image-rendering:pixelated}}"
            "td,th{{padding:2px}}</style><h1>{0}</h1>"
            "<table><tr><th>seed</th>{1}</tr>{2}</table>\n").format(html.escape(title), header,
                                                                   "".join(rows))
    (out / "index.html").write_text(page, encoding="utf-8")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wgcli", required=True, help="path of the wgcli executable")
    parser.add_argument("--out", required=True, type=Path, help="output directory")
    parser.add_argument("--seeds", default="1..6", help="seeds such as 1..6 or 1,5,9")
    parser.add_argument("--size", type=int, default=257, help="grid size N (default 257)")
    parser.add_argument("--profile", default="classic", help="profile name or file")
    parser.add_argument("--params", help="preset name or params file (default: schema defaults)")
    parser.add_argument("--keep-worlds", action="store_true", help="keep the .wgw files")
    args = parser.parse_args(argv)
    try:
        seeds = parse_seeds(args.seeds)
    except ValueError as e:
        print("error: --seeds: {}".format(e), file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    ok = [s for s in seeds if export_seed(args.wgcli, args.out, s, args)]
    write_index(args.out, seeds, ok, args)
    print("{} of {} seeds exported to {}".format(len(ok), len(seeds), args.out / "index.html"))
    return 0 if len(ok) == len(seeds) else 1


if __name__ == "__main__":
    sys.exit(main())
