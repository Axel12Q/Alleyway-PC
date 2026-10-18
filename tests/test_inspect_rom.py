"""Synthetic cartridge headers only; no commercial data."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

spec = importlib.util.spec_from_file_location("inspect_rom", ROOT / "tools/inspect_rom.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

synth_spec = importlib.util.spec_from_file_location("synthetic_rom", ROOT / "tools/synthetic_rom.py")
synth_module = importlib.util.module_from_spec(synth_spec)
synth_spec.loader.exec_module(synth_module)


class HeaderTests(unittest.TestCase):
    def test_rejects_truncated_file(self):
        with self.assertRaises(ValueError):
            module.inspect(bytes(32))

    def test_rejects_non_bytes_input(self):
        with self.assertRaises(TypeError):
            module.inspect("not-bytes")  # type: ignore[arg-type]

    def test_checksums_and_corruption(self):
        data = bytearray(synth_module.build_synthetic_rom(title="SYNTHETIC", interactive=True))
        report = module.inspect(data)
        self.assertEqual(report["title"], "SYNTHETIC")
        self.assertTrue(report["header_checksum_valid"])
        self.assertTrue(report["global_checksum_valid"])
        self.assertTrue(report["bank_aligned"])
        self.assertTrue(report["rom_size_matches_header"])
        self.assertTrue(report["pyboy_loadable"])
        self.assertFalse(report["identity_verified"])

        data[0x134] ^= 1
        report = module.inspect(data)
        self.assertFalse(report["header_checksum_valid"])
        self.assertFalse(report["global_checksum_valid"])
        self.assertFalse(report["pyboy_loadable"])

    def test_detects_unaligned_rom_size(self):
        data = bytearray(synth_module.build_synthetic_rom(title="UNALIGNED"))
        data.append(0x00)
        report = module.inspect(data)
        self.assertFalse(report["bank_aligned"])
        self.assertFalse(report["rom_size_matches_header"])
        self.assertFalse(report["pyboy_loadable"])


if __name__ == "__main__":
    unittest.main()
