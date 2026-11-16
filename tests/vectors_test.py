"""The reference vectors name the retail file they were cut from."""
import hashlib
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RETAIL = Path.home() / "Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit"
FROZEN = "1d0d453"


class Vectors(unittest.TestCase):
    def headers(self):
        return sorted((ROOT / "tests/vectors").glob("*_vectors.h"))

    def test_vectors_name_their_source(self):
        self.assertTrue(self.headers())
        for header in self.headers():
            text = header.read_text()
            self.assertIn(f'_VECTOR_COMMIT "{FROZEN}"', text, header.name)
            self.assertRegex(text, r'_VECTOR_SHA256 "[0-9a-f]{64}"')

    @unittest.skipUnless((RETAIL / "src/first_person").is_dir(), "retail source not on this machine")
    def test_vectors_match_the_retail_files_on_disk(self):
        for header in self.headers():
            text = header.read_text()
            name = re.search(r"Retail file: (\S+) at commit", text)[1]
            want = re.search(r'_VECTOR_SHA256 "([0-9a-f]{64})"', text)[1]
            blob = subprocess.check_output(["git", "-C", str(RETAIL), "show", f"{FROZEN}:Goose Gadget Kit/{name}"])
            self.assertEqual(hashlib.sha256(blob).hexdigest(), want, name)

    def test_a_c_test_refuses_vectors_from_another_commit(self):
        for name in ("transition", "recovery"):
            program = ROOT / "work/tests" / name
            if program.exists():
                self.assertEqual(subprocess.run([str(program), "0000000"], capture_output=True).returncode, 2)


if __name__ == "__main__":
    unittest.main()
