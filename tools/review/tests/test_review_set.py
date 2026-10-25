"""Tests for tools/review/review_set.py (A23).

Run from the repository root: python -m unittest discover -s tools/review/tests -v
"""

import argparse
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import review_set as rs  # noqa: E402


class ParseSeeds(unittest.TestCase):
    def test_ranges_and_lists(self):
        self.assertEqual(rs.parse_seeds("1..3"), [1, 2, 3])
        self.assertEqual(rs.parse_seeds("1,5,9"), [1, 5, 9])
        self.assertEqual(rs.parse_seeds("1..2,7"), [1, 2, 7])

    def test_invalid(self):
        for text in ("", "a", "1..b"):
            with self.assertRaises(ValueError):
                rs.parse_seeds(text)


class WriteIndex(unittest.TestCase):
    def test_one_row_per_seed_and_one_image_per_field(self):
        args = argparse.Namespace(profile="classic", size=257, params=None)
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            rs.write_index(out, [1, 2], [1], args)
            page = (out / "index.html").read_text(encoding="utf-8")
        self.assertIn("Review set: classic N = 257", page)
        self.assertEqual(page.count("<img "), len(rs.FIELDS))
        self.assertIn("seed_1/elevation_terrain.png", page)
        self.assertIn("seed_1/biome_viridis.png", page)
        self.assertIn("<th>2</th><td colspan='{}'>failed</td>".format(len(rs.FIELDS)), page)

    def test_fields_have_a_colormap_wgcli_knows(self):
        for field, colormap in rs.FIELDS:
            self.assertIn(colormap, ("grey", "terrain", "viridis"), field)


if __name__ == "__main__":
    unittest.main()
