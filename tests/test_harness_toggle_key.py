"""The harness presses the mod's toggle key, U since round 36: F11 is HellForge 0.5's Armor LUT editor."""
import re
import sys
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parents[1] / "tools" / "harness"
sys.path.insert(0, str(HARNESS))
import drive
import matrix


class ToggleKey(unittest.TestCase):
    def test_the_toggle_key_is_u(self):
        self.assertEqual(drive.TOGGLE, "u")
        self.assertEqual(drive._code(drive.TOGGLE), str(0x55))

    def test_f11_still_has_a_name(self):
        # kept so a script can still reach HellForge's own F11 (its Armor LUT editor) on purpose
        self.assertEqual(drive._code("f11"), str(0x7A))

    def test_the_toggle_phases_press_u(self):
        self.assertEqual(matrix.PHASES["toggle-off-on"][0], "key u; wait 1.0; key u")
        self.assertEqual(matrix.PHASES["toggle-fast"][0].count("key u"), 4)

    def test_no_harness_script_toggles_with_f11(self):
        pressed = []
        for path in sorted(HARNESS.iterdir()):
            if path.suffix in (".py", ".sh", ".lua"):
                for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                    if re.search(r"\bkey f11\b|key:f11", line, re.I):
                        pressed.append(f"{path.name}:{number}: {line.strip()}")
        self.assertEqual(pressed, [])


if __name__ == "__main__":
    unittest.main()
