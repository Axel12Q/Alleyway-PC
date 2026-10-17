"""Read cartridge metadata. Does not copy or extract the ROM."""
import argparse
import hashlib
import json
from pathlib import Path

def inspect(data):
    if len(data) < 0x150:
        raise ValueError("File too small for a Game Boy cartridge header")
    checksum = 0
    for value in data[0x134:0x14D]:
        checksum = (checksum - value - 1) & 255
    return {
        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
        "title": data[0x134:0x143].split(b"\0")[0].decode("ascii", errors="replace"),
        "cartridge_type": data[0x147], "rom_size_code": data[0x148],
        "ram_size_code": data[0x149], "version": data[0x14C],
        "header_checksum_valid": checksum == data[0x14D],
        "global_checksum_valid": ((sum(data) - data[0x14E] - data[0x14F]) & 65535)
            == int.from_bytes(data[0x14E:0x150], "big"),
        "identity_verified": False,
    }

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.rom.read_bytes()), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")
