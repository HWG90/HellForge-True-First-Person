"""Session report over a recorded log; never a game."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/smooth_report.py"
LOG = ROOT / "tests/reference/smooth-session.log"
HIP = "queue=[4*] base=4 mask=0 active=0 suppress=0 aim=0 ragdoll=0 seat=0"
RIG = "queue=[4*,6*] base=4 mask=0 active=1 suppress=0 aim=0 ragdoll=0 seat=0"
STOW = "queue=[4*] base=4 mask=2 active=1 suppress=0 aim=0 ragdoll=0 seat=0"


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)


class Report(unittest.TestCase):
    def data(self):
        result = run(LOG, "--json")
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_request_states(self):
        states = {row["state"]: row for row in self.data()["states"]}
        self.assertEqual(set(states), {HIP, RIG, STOW})
        self.assertEqual((states[HIP]["entries"], states[HIP]["total_ms"], states[HIP]["longest_ms"]), (2, 2000, 1000))
        self.assertEqual((states[RIG]["entries"], states[RIG]["total_ms"], states[RIG]["longest_ms"]), (2, 3050, 2000))
        self.assertEqual((states[STOW]["entries"], states[STOW]["total_ms"], states[STOW]["longest_ms"]), (1, 1000, 1000))

    def test_jumps_belong_to_the_state_active_at_their_time(self):
        data = self.data()
        jumps = {row["state"]: row for row in data["jumps"]}
        self.assertEqual(sum(row["frames"] for row in data["jumps"]), 6)
        self.assertEqual(jumps[HIP]["count"], {"E": 1})
        self.assertEqual(jumps[STOW]["count"], {"E": 2, "S": 1, "T": 1})
        self.assertEqual(jumps[STOW]["max"]["E"], 0.440)
        self.assertEqual(jumps[STOW]["max"]["S"], 0.371)
        self.assertEqual(jumps[STOW]["max"]["T"], 46.0)
        self.assertEqual(jumps[STOW]["released"], 2)
        self.assertEqual(jumps[RIG]["count"], {"F": 1, "G": 1})
        self.assertEqual(jumps[RIG]["max"]["F"], 30.0)
        self.assertEqual((data["dropped"], data["held"]), (3, 2))

    def test_text_tables(self):
        result = run(LOG)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Request states", result.stdout)
        self.assertIn("Jumps", result.stdout)
        self.assertIn(STOW, result.stdout)
        self.assertIn("0.440", result.stdout)
        self.assertTrue(result.stdout.rstrip().endswith("dropped=3 held=2"))

    def test_log_without_matching_lines(self):
        with tempfile.NamedTemporaryFile("w", suffix=".log") as empty:
            empty.write("nothing to see\n")
            empty.flush()
            result = run(empty.name, "--json")
            self.assertEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stdout), {"states": [], "jumps": [], "dropped": 0, "held": 0})

    def test_missing_file(self):
        self.assertEqual(run(ROOT / "no-such.log").returncode, 2)


if __name__ == "__main__":
    unittest.main()
