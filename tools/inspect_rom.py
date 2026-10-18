"""Read cartridge metadata. Does not copy or extract the ROM."""
import argparse
import hashlib
import json
from pathlib import Path

ROM_SIZE_MAP = {
    0x00: 32 * 1024,
    0x01: 64 * 1024,
    0x02: 128 * 1024,
    0x03: 256 * 1024,
    0x04: 512 * 1024,
    0x05: 1024 * 1024,
    0x06: 2048 * 1024,
    0x07: 4096 * 1024,
    0x08: 8192 * 1024,
}

RAM_SIZE_MAP = {
    0x00: 0,
    0x01: 2 * 1024,
    0x02: 8 * 1024,
    0x03: 32 * 1024,
    0x04: 128 * 1024,
    0x05: 64 * 1024,
}


def inspect(data: bytes | bytearray) -> dict:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("ROM data must be a bytes-like object")
    raw = bytes(data)
    if len(raw) < 0x150:
        raise ValueError("File too small for a Game Boy cartridge header")

    header_checksum = 0
    for value in raw[0x134:0x14D]:
        header_checksum = (header_checksum - value - 1) & 255
    stored_header_checksum = raw[0x14D]
    header_valid = header_checksum == stored_header_checksum

    expected_global = (sum(raw) - raw[0x14E] - raw[0x14F]) & 65535
    stored_global = int.from_bytes(raw[0x14E:0x150], "big")
    global_valid = expected_global == stored_global

    rom_size_code = raw[0x148]
    ram_size_code = raw[0x149]
    expected_rom_bytes = ROM_SIZE_MAP.get(rom_size_code)
    expected_ram_bytes = RAM_SIZE_MAP.get(ram_size_code)
    bank_aligned = (len(raw) % 16384) == 0

    return {
        "sha256": hashlib.sha256(raw).hexdigest(),
        "bytes": len(raw),
        "title": raw[0x134:0x143].split(b"\0")[0].decode("ascii", errors="replace"),
        "cgb_flag": raw[0x143],
        "new_licensee_code": raw[0x144:0x146].split(b"\0")[0].decode("ascii", errors="replace"),
        "sgb_flag": raw[0x146],
        "cartridge_type": raw[0x147],
        "rom_size_code": rom_size_code,
        "expected_rom_bytes": expected_rom_bytes,
        "rom_size_matches_header": expected_rom_bytes == len(raw),
        "bank_aligned": bank_aligned,
        "ram_size_code": ram_size_code,
        "expected_ram_bytes": expected_ram_bytes,
        "destination_code": raw[0x14A],
        "old_licensee_code": raw[0x14B],
        "version": raw[0x14C],
        "header_checksum_expected": header_checksum,
        "header_checksum_stored": stored_header_checksum,
        "header_checksum_valid": header_valid,
        "global_checksum_expected": expected_global,
        "global_checksum_stored": stored_global,
        "global_checksum_valid": global_valid,
        "logo_non_zero": any(raw[0x104:0x134]),
        "pyboy_loadable": bank_aligned and header_valid,
        "identity_verified": False,
    }


def inspect_file(path: Path) -> dict:
    if not path.is_file():
        raise FileNotFoundError(f"ROM file not found: {path}")
    return inspect(path.read_bytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_file(args.rom), indent=2))
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Error: {error}\n")
