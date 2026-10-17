"""Build Windows diagnostic using the project-local Zig toolchain."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "build"
out.mkdir(exist_ok=True)
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-O2", str(root / "src/main.c"),
                "-o", str(out / "alleyway-lab.exe"), "-luser32", "-lgdi32"], check=True)
print(out / "alleyway-lab.exe")
