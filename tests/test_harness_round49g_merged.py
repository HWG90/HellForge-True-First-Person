"""Round 49g merged with main (round 49a's turn and veil judges): every phase and every soak action keeps the turn
snap and veil fault checks beside the stance class and the landing measure, and the soak flags all of them."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
sys.path.insert(0, str(ROOT / "tests"))
import matrix  # noqa: E402
import soak  # noqa: E402

TURN = ROOT / "tests/reference/turn"
PRONE_BACK = TURN / "soak62-C-L1960-2012.log"   # 72.7 degrees at a change of camera
FLICK = TURN / "soak60-A-L6760-6800.log"        # the veil 0.248 m, then nothing
LANDING = ROOT / "tests/reference/landing/feel58-soak51B-232320.log"


class Merged(unittest.TestCase):
    def test_every_phase_keeps_the_turn_and_the_veil(self):
        lines = PRONE_BACK.read_text().splitlines()
        for name, (script, _) in matrix.PHASES.items():
            j = matrix.phase_judge(name, script, lines)
            for key in ("turn_snaps", "turn_deg", "veil_faults", "veil_open_m", "said", "stance_frames"):
                self.assertIn(key, j, name)
            self.assertEqual([round(t["degrees"], 1) for t in j["turn_snaps"]], [72.7], name)
            looked = "look" in script or name.startswith("toggle")
            self.assertEqual(any("72.7 degrees" in n and "not counted" in n for n in j["notes"]), looked, name)
            if not looked:
                self.assertFalse(j["pass"], name)

    def test_a_vault_phase_has_both_the_landing_and_the_turn(self):
        lines = LANDING.read_text().splitlines()
        j = matrix.phase_judge("jump-vault", matrix.PHASES["jump-vault"][0], lines)
        self.assertIn("landing_m", j)
        self.assertIn("landing_drop_m", j)
        self.assertIn("turn_snaps", j)
        self.assertTrue(any(n.startswith("landing") for n in j["notes"]))
        self.assertFalse(any(s.startswith("landing") for s in j["said"]))   # the soak prints the two apart, once each

    def test_the_soak_judges_and_flags_the_turn_and_the_veil(self):
        lines = PRONE_BACK.read_text().splitlines()
        plain = soak.judged("walk", "hold w 1.5", lines)
        self.assertTrue(plain["turn_snaps"])
        self.assertTrue(soak.flagged({**plain, "largest_jump_m": 0.0, "missed": 0, "notes": [], "veil_faults": []}, "hold w 1.5"))
        looked = soak.judged("look", matrix.PHASES["look"][0], lines)
        self.assertTrue(any("not counted" in s for s in looked["said"]))
        self.assertFalse(soak.flagged({**looked, "largest_jump_m": 0.0, "missed": 0, "notes": [], "veil_faults": []}, matrix.PHASES["look"][0]))
        flick = soak.judged("walk", "hold w 1.5", FLICK.read_text().splitlines())
        self.assertTrue(flick["veil_faults"])
        self.assertTrue(soak.flagged({**flick, "largest_jump_m": 0.0, "missed": 0, "notes": [], "turn_snaps": []}, "hold w 1.5"))
        clean = {**plain, "largest_jump_m": 0.0, "missed": 0, "notes": [], "veil_faults": [], "turn_snaps": []}
        self.assertFalse(soak.flagged(clean, "hold w 1.5"))


if __name__ == "__main__":
    unittest.main()
