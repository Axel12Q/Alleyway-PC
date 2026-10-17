"""Capture deterministic no-input reference frames from a user supplied ROM."""
import argparse
import hashlib
import json
from importlib.metadata import version
from pathlib import Path
from pyboy import PyBoy

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--frames", type=int, default=120)
    args = parser.parse_args()
    if not args.rom.is_file() or args.frames < 1:
        parser.error("Provide an existing ROM and a positive frame count")
    digest = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    root = Path(__file__).resolve().parents[1]
    out = root / "local/captures" / digest[:12] / f"frame-{args.frames}"
    out.mkdir(parents=True, exist_ok=True)
    gb = PyBoy(str(args.rom), window="null", sound_emulated=False)
    try:
        gb.set_emulation_speed(0)
        if not gb.tick(args.frames, True):
            raise RuntimeError("Emulator stopped before capture completed")
        rgba = gb.screen.ndarray.copy()
        # PPM avoids adding Pillow solely for image export.
        (out / "screen.ppm").write_bytes(b"P6\n160 144\n255\n" + rgba[:, :, :3].tobytes())
        (out / "capture.json").write_text(json.dumps({
            "rom_sha256": digest, "frames": args.frames, "inputs": [],
            "pyboy_version": version("pyboy"),
            "screen_sha256": hashlib.sha256(rgba.tobytes()).hexdigest(),
            "sound_emulated": False, "bootrom": "PyBoy default",
            "note": "Boot capture only. No Alleyway addresses identified yet."
        }, indent=2), encoding="utf-8")
    finally:
        gb.stop(save=False)
    print(out)

if __name__ == "__main__":
    main()
