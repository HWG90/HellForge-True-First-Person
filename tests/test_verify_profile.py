"""Pins of the build profile against a game image; never a game."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/verify_profile.py"
HEADER = '''
static const FpWitness fp_witnesses[] = {
    {"first",UINT32_C(0x100),"48895c24"},
    {"second",UINT32_C(0x200),"41ff5138"},
};
static const FpNativeSite fp_native_sites[]={
{"in_exe",true,UINT32_C(0x300),"f30f104134c3"},
{"in_game",false,UINT32_C(0x400),"ff9280000000"},
{"masked",false,UINT32_C(0x500),"488b0d??????"+"??e8"},
};
'''.replace('"+"', "")


def image(changes=()):
    data = bytearray(0x1000)
    for at, hexa in ((0x100, "48895c24"), (0x200, "41ff5138"), (0x400, "ff9280000000"), (0x500, "488b0d11223344e8")):
        data[at:at + len(hexa) // 2] = bytes.fromhex(hexa)
    for at, value in changes:
        data[at] = value
    return bytes(data)


def exe_image():
    data = bytearray(0x1000)
    data[0x300:0x306] = bytes.fromhex("f30f104134c3")
    return bytes(data)


class Verify(unittest.TestCase):
    def run_tool(self, game, exe=None, header=HEADER):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "p.h").write_text(header)
            (folder / "game.bin").write_bytes(game)
            command = [sys.executable, str(TOOL), "--image", str(folder / "game.bin"), "--header", str(folder / "p.h")]
            if exe is not None:
                (folder / "exe.bin").write_bytes(exe)
                command += ["--exe-image", str(folder / "exe.bin")]
            return subprocess.run(command, capture_output=True, text=True)

    def test_all_pins_match(self):
        result = self.run_tool(image(), exe_image())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("ok "), 5)

    def test_wrong_build_is_refused(self):
        result = self.run_tool(image([(0x102, 0)]), exe_image())
        self.assertEqual(result.returncode, 1)
        self.assertIn("MISMATCH first 0x100 at byte 2", result.stdout)

    def test_changed_pin_is_named(self):
        result = self.run_tool(image([(0x405, 0x90)]), exe_image())
        self.assertEqual(result.returncode, 1)
        self.assertIn("MISMATCH in_game 0x400 at byte 5", result.stdout)
        self.assertIn("ok first", result.stdout)

    def test_wildcards_are_skipped(self):
        result = self.run_tool(image([(0x503, 0x99), (0x506, 0x77)]), exe_image())
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.run_tool(image([(0x507, 0x00)]), exe_image())
        self.assertIn("MISMATCH masked 0x500 at byte 7", result.stdout)

    def test_pin_outside_image_is_refused(self):
        header = HEADER.replace("UINT32_C(0x400)", "UINT32_C(0xfff)")
        result = self.run_tool(image(), exe_image(), header)
        self.assertEqual(result.returncode, 1)
        self.assertIn("OUTSIDE in_game 0xfff", result.stdout)

    def test_exe_pins_need_the_exe_image(self):
        result = self.run_tool(image())
        self.assertEqual(result.returncode, 1)
        self.assertIn("UNCHECKED in_exe 0x300 (no executable image given)", result.stdout)

    def test_header_without_pins_is_an_error(self):
        result = self.run_tool(image(), exe_image(), "/* nothing */")
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
