"""Local launcher for Alleyway PC research checkpoint.

Launches the native C executable with a local user ROM if found, or in diagnostic
mode if no ROM is present. Does not copy, move, or publish ROM files or modify any saves.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE_PATH = ROOT / "build/alleyway-lab.exe"

CANDIDATES = [
    ROOT / "local/roms/Alleyway (USA).gb",
    ROOT / "local/roms/alleyway.gb",
    ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb",
    ROOT.parent / "Local-Privado/roms/alleyway.gb",
]


def find_rom():
    if len(sys.argv) > 1:
        p = Path(sys.argv[1]).resolve()
        if p.is_file():
            return p
        print(f"Warning: specified ROM path not found: {sys.argv[1]}")
    for c in CANDIDATES:
        if c.is_file():
            return c.resolve()
    return None


def main():
    if not EXE_PATH.is_file():
        print("Executable not found. Building with tools/build.py...")
        subprocess.run([sys.executable, str(ROOT / "tools/build.py")], check=True)

    rom = find_rom()

    print("=" * 70)
    print(" Alleyway PC | Native DMG Reimplementation (Checkpoint)")
    print("=" * 70)

    if rom:
        print(f"ROM detected: {rom}")
        print("Starting in authentic ROM mode (Title screen 'PUSH START').")
    else:
        print("Notice: No commercial ROM found at candidate locations:")
        for c in CANDIDATES:
            print(f"  - {c}")
        print()
        print("Starting in DIAGNOSTIC FALLBACK mode (pattern display, press Esc to exit).")
        print("To run with authentic assets, provide your ROM:")
        print("  python tools/launch.py \"path/to/Alleyway (USA).gb\"")
        print("  or place it in 'local/roms/' or '../Local-Privado/roms/' (ignored by Git).")

    print("-" * 70)
    print("Controls (Win32 Window):")
    print("  Enter         : Start game (from Title) / Pause / Resume / Restart")
    print("  Space / Z     : Button A (Serve ball / Accelerate paddle to 5 px/frame)")
    print("  Left / Right  : Move paddle (standard 3 px/frame; or A / D keys)")
    print("  Shift / X     : Button B (Precision paddle movement: 1 px/frame)")
    print("  R             : Reset directly to Stage 1 Serve")
    print("  Esc           : Close window and exit cleanly")
    print("=" * 70)

    cmd = [str(EXE_PATH)]
    if rom:
        cmd.extend(["--rom", str(rom)])

    try:
        proc = subprocess.run(cmd)
        sys.exit(proc.returncode)
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
