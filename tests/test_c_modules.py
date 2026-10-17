"""
test_c_modules.py: Automated runner for the compiled C module verification harness.
"""

from pathlib import Path
import subprocess
import sys
import unittest


class NativeCModulesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.build_dir = cls.root / "build"
        cls.build_dir.mkdir(exist_ok=True)
        cls.exe_path = cls.build_dir / "test_c_modules.exe"

        # Compile test_c_modules.c with project-local Zig
        cmd = [
            sys.executable, "-m", "ziglang", "cc",
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
            f"-I{cls.root / 'src'}",
            str(cls.root / "tests" / "test_c_modules.c"),
            "-o", str(cls.exe_path)
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Failed to compile test_c_modules.c:\n{res.stderr}")

    def test_c_tile_and_apu_modules_pass(self):
        res = subprocess.run([str(self.exe_path)], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"C module tests failed:\n{res.stderr}")
        self.assertIn("ALL C TESTS PASSED", res.stdout)

    def test_c_deliberate_fail_detected(self):
        res = subprocess.run([str(self.exe_path), "--deliberate-fail"], capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Intentional failure mode", res.stderr)


if __name__ == '__main__':
    unittest.main()
