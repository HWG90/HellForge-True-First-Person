"""Composition and inversion of the retail kit's build-migration records."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "tools/build_map.py"


def kit(folder):
    docs = folder / "docs"
    docs.mkdir()
    (docs / "first-person-applied-migration.json").write_text(json.dumps(
        {"constants": {"0x10": "0x20", "0x11": "0x21", "0x12": "0x12", "0x13": "0x23", "0x14": "0x23"}}))
    (docs / "first-person-26251122-migration.json").write_text(json.dumps(
        {"constants": {"0x20": "0x30", "0x90": "0x91"}}))
    (docs / "first-person-26568081-migration.json").write_text(json.dumps(
        {"constants": {"0x30": "0x40", "0x21": "0x41", "0x91": "0x92", "0xa0": "0xa1"}}))


class BuildMap(unittest.TestCase):
    def run_tool(self, *args):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            kit(folder)
            out = folder / "table.md"
            result = subprocess.run([sys.executable, str(TOOL), "--retail", str(folder), "--write", str(out), *args],
                                    capture_output=True, text=True)
            return result, out.read_text()

    def test_three_moves_compose_and_invert(self):
        result, table = self.run_tool("--to-here", "0x40", "0x41", "0x12")
        self.assertEqual(result.stdout.split("\n")[:3], ["0x40 -> 0x10", "0x41 -> 0x11", "0x12 -> 0x12"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("| `0x10` | `0x40` | yes |", table)
        self.assertIn("| `0x12` | `0x12` |  |", table)

    def test_later_constants_are_listed_not_guessed(self):
        result, table = self.run_tool("--to-here", "0x92", "0xa1", "0x77")
        self.assertEqual(result.returncode, 1)
        self.assertIn("0x92 -> UNKNOWN (added on 26251122)", result.stdout)
        self.assertIn("0xa1 -> UNKNOWN (added on 26568081)", result.stdout)
        self.assertIn("0x77 -> UNKNOWN (not in the records)", result.stdout)
        self.assertIn("| `0x92` | 26251122 |", table)

    def test_two_values_here_for_one_retail_value_are_ambiguous(self):
        result, table = self.run_tool("--to-here", "0x23")
        self.assertEqual(result.returncode, 1)
        self.assertIn("0x23 -> AMBIGUOUS", result.stdout)
        self.assertIn("ambiguous on the way back", table)


if __name__ == "__main__":
    unittest.main()
