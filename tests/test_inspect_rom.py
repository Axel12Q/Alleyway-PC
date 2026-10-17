"""Synthetic cartridge headers only; no commercial data."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("inspect_rom", Path(__file__).resolve().parents[1] / "tools/inspect_rom.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class HeaderTests(unittest.TestCase):
    def test_rejects_truncated_file(self):
        with self.assertRaises(ValueError):
            module.inspect(bytes(32))

    def test_checksums_and_corruption(self):
        data = bytearray(32768)
        data[0x134:0x13D] = b"SYNTHETIC"
        check = 0
        for value in data[0x134:0x14D]:
            check = (check - value - 1) & 255
        data[0x14D] = check
        data[0x14E:0x150] = (sum(data) & 65535).to_bytes(2, "big")
        report = module.inspect(data)
        self.assertEqual(report["title"], "SYNTHETIC")
        self.assertTrue(report["header_checksum_valid"])
        self.assertTrue(report["global_checksum_valid"])
        data[0x134] ^= 1
        report = module.inspect(data)
        self.assertFalse(report["header_checksum_valid"])
        self.assertFalse(report["global_checksum_valid"])

if __name__ == "__main__":
    unittest.main()
