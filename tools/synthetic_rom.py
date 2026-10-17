"""Build synthetic Game Boy test cartridges in memory or on disk.

All code and memory addresses here are synthetic test fixtures for verifying
our capture, input-timeline, RAM-scanner, and trace-comparison tools.
They do NOT represent Alleyway game logic or Alleyway RAM addresses.
"""
import argparse
from pathlib import Path


# Synthetic fixture WRAM addresses (for tool verification only, NOT Alleyway):
# 0xC000: frame counter (incremented once per VBlank after PyBoy bootrom ends)
# 0xC001: synthetic X coordinate (starts at 80; Right increments, Left decrements; copied to SCX)
# 0xC002: synthetic Y coordinate (starts at 72; Down increments, Up decrements; copied to SCY)
# 0xC003: active button bitmask (Bit 0=A, Bit 1=B, Bit 2=Select, Bit 3=Start)
# 0xC004: button-A hold frame counter (incremented each frame A is held)
SYNTHETIC_WATCH_SPEC = {
    "frame_counter": "0xC000:u8",
    "synth_x": "0xC001:u8",
    "synth_y": "0xC002:u8",
    "buttons_mask": "0xC003:u8",
    "button_a_ticks": "0xC004:u8",
}


def _interactive_program() -> bytes:
    """Assemble the VBlank-synced joypad/WRAM synthetic test loop at 0x0150."""
    return bytes([
        # Init synthetic state at 0x0150:
        0xAF,                         # XOR A
        0xEA, 0x00, 0xC0,             # LD (0xC000), A ; frame_counter = 0
        0xEA, 0x03, 0xC0,             # LD (0xC003), A ; buttons_mask = 0
        0xEA, 0x04, 0xC0,             # LD (0xC004), A ; button_a_ticks = 0
        0x3E, 0x50, 0xEA, 0x01, 0xC0, # LD A, 80; LD (0xC001), A ; synth_x = 80
        0x3E, 0x48, 0xEA, 0x02, 0xC0, # LD A, 72; LD (0xC002), A ; synth_y = 72
        # Main loop at 0x0164:
        0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, # wait_vblank: LDH A,(0x44); CP 144; JR NZ,-6
        0x21, 0x00, 0xC0, 0x34,             # LD HL, 0xC000; INC (HL)
        # Read D-pad (select P14 low = 0x20)
        0x3E, 0x20, 0xE0, 0x00,             # LD A, 0x20; LDH (0x00), A
        0xF0, 0x00, 0xF0, 0x00,             # LDH A, (0x00) twice
        0x2F, 0xE6, 0x0F, 0x47,             # CPL; AND 0x0F; LD B, A
        0x21, 0x01, 0xC0,                   # LD HL, 0xC001 (synth_x)
        0xCB, 0x40, 0x28, 0x01, 0x34,       # BIT 0, B (Right); JR Z,+1; INC (HL)
        0xCB, 0x48, 0x28, 0x01, 0x35,       # BIT 1, B (Left);  JR Z,+1; DEC (HL)
        0x7E, 0xE0, 0x43,                   # LD A, (HL); LDH (0x43), A (SCX)
        0x23,                               # INC HL -> 0xC002 (synth_y)
        0xCB, 0x50, 0x28, 0x01, 0x35,       # BIT 2, B (Up);    JR Z,+1; DEC (HL)
        0xCB, 0x58, 0x28, 0x01, 0x34,       # BIT 3, B (Down);  JR Z,+1; INC (HL)
        0x7E, 0xE0, 0x42,                   # LD A, (HL); LDH (0x42), A (SCY)
        # Read Action buttons (select P15 low = 0x10)
        0x3E, 0x10, 0xE0, 0x00,             # LD A, 0x10; LDH (0x00), A
        0xF0, 0x00, 0xF0, 0x00,             # LDH A, (0x00) twice
        0x2F, 0xE6, 0x0F,                   # CPL; AND 0x0F
        0xEA, 0x03, 0xC0,                   # LD (0xC003), A (buttons_mask)
        0xCB, 0x47, 0x28, 0x04,             # BIT 0, A (Button A); JR Z,+4
        0x21, 0x04, 0xC0, 0x34,             # LD HL, 0xC004; INC (HL)
        # Deselect joypad lines
        0x3E, 0x30, 0xE0, 0x00,             # LD A, 0x30; LDH (0x00), A
        # Wait for VBlank line 144 to end so we increment once per frame
        0xF0, 0x44, 0xFE, 0x90, 0x28, 0xFA, # wait_line145: LDH A,(0x44); CP 144; JR Z,-6
        0xC3, 0x64, 0x01,                   # JP 0x0164
    ])


def build_synthetic_rom(title: str = "SYNTHETIC", interactive: bool = True) -> bytes:
    """Create a 32 KiB synthetic ROM image with valid header and global checksums."""
    rom = bytearray(32768)
    rom[0x0100:0x0104] = b"\x00\xC3\x50\x01"  # NOP; JP 0x0150
    encoded_title = title.encode("ascii")[:15]
    rom[0x0134:0x0134 + len(encoded_title)] = encoded_title
    program = _interactive_program() if interactive else b"\x18\xFE"
    rom[0x0150:0x0150 + len(program)] = program

    header_checksum = 0
    for value in rom[0x0134:0x014D]:
        header_checksum = (header_checksum - value - 1) & 0xFF
    rom[0x014D] = header_checksum

    global_checksum = (sum(rom) - rom[0x014E] - rom[0x014F]) & 0xFFFF
    rom[0x014E:0x0150] = global_checksum.to_bytes(2, "big")
    return bytes(rom)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "local/roms/synthetic.gb",
        help="Destination path for synthetic test ROM (inside ignored local/ directory)",
    )
    parser.add_argument(
        "--minimal",
        action="store_true",
        help="Generate minimal infinite-loop ROM instead of interactive joypad fixture",
    )
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = build_synthetic_rom(interactive=not args.minimal)
    args.output.write_bytes(data)
    print(args.output)


if __name__ == "__main__":
    main()
