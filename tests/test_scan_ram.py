"""Unit tests for differential RAM scanner using synthetic ROM fixtures only."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def _load_tool(name: str, rel_path: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


synth_mod = _load_tool("synthetic_rom", "tools/synthetic_rom.py")
scan_mod = _load_tool("scan_ram", "tools/scan_ram.py")


class RamScannerTests(unittest.TestCase):
    def test_discovers_synthetic_x_and_button_addresses(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom_path = Path(tmp) / "synthetic.gb"
            rom_path.write_bytes(synth_mod.build_synthetic_rom(interactive=True))

            inputs_idle = []
            inputs_move = [
                {"frame": 66, "button": "right", "action": "press"},
                {"frame": 67, "button": "a", "action": "press"},
                {"frame": 70, "button": "a", "action": "release"},
                {"frame": 71, "button": "right", "action": "release"},
            ]

            diffs = scan_mod.diff_timelines(
                rom_path=rom_path,
                frames=73,
                inputs_a=inputs_idle,
                inputs_b=inputs_move,
                regions=("wram",),
            )
            by_addr = {d["address"]: d for d in diffs}
            # 0xC001 is synthetic X (+5 difference), 0xC004 is button A hold counter (+3 difference)
            self.assertIn(0xC001, by_addr)
            self.assertEqual(by_addr[0xC001]["delta"], 5)
            self.assertIn(0xC004, by_addr)
            self.assertEqual(by_addr[0xC004]["delta"], 3)

            # Test find_correlated_addresses across sample frames
            sample_frames = [65, 67, 69, 72]
            snaps = scan_mod.capture_snapshots(
                rom_path=rom_path,
                sample_frames=sample_frames,
                inputs=inputs_move,
                regions=("wram", "io"),
            )
            scx_series = [snaps[f][0xFF43] for f in sample_frames]
            correlated = scan_mod.find_correlated_addresses(
                snapshots=snaps,
                sample_frames=sample_frames,
                target_values=scx_series,
                search_regions=("wram",),
            )
            corr_addrs = [c["address"] for c in correlated]
            self.assertIn(0xC001, corr_addrs)


if __name__ == "__main__":
    unittest.main()
