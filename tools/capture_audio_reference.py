"""Local-ROM oracle for the title-specific channel-1 sequencer.

Injects one primary SFX request at the sound-driver entry and observes its actual
WRAM state and register WRITES (NR13 is write-only). No expected traces are stored
in the repository. Does not compare PCM phase or claim full APU fidelity.
"""
from pyboy import PyBoy


def capture(rom_path, effect, frames=160):
    gb = PyBoy(str(rom_path),window="null",sound_emulated=True)
    gb.set_emulation_speed(0)
    gb.tick(120)
    registers=[0]*5
    records=[]
    initial=True

    def start(_):
        nonlocal initial
        if initial:
            gb.memory[0xDFE0]=effect
            for address in (0xDFE2,0xDFE6,0xDFE9,0xDFFB,0xDFFD,0xDFFC,0xDFFE,0xDFD7):
                gb.memory[address]=0
            initial=False

    def end(_):
        records.append([int(gb.memory[a]) for a in
                        (0xDFE2,0xDFE6,0xDFE9,0xDFFB,0xDFFD,0xDFFC,0xDFFE)]+registers.copy())

    def write(register):
        if register is None:
            register=gb.register_file.C
        if 0x10<=register<=0x14:
            registers[register-0x10]=gb.register_file.A

    # Verified instruction addresses, not extracted sound data.
    writes=((0x6DFF,0x10),(0x6E03,0x11),(0x6E07,0x12),(0x6E2C,0x13),(0x6E31,0x14),
            (0x6E55,0x13),(0x6E5A,0x14),(0x6E61,0x12),(0x6E72,0x10),(0x6E76,0x11),
            (0x6E7A,0x12),(0x6E9F,0x13),(0x6EA4,0x14),(0x6EC8,0x13),(0x6ECD,0x14),
            (0x6ED4,0x12),(0x6EDE,0x10),(0x6EE2,0x11),(0x6EE6,0x12),(0x6F03,0x13),
            (0x6F08,0x14),(0x6F0F,0x12),(0x6F24,0x10),(0x6F28,0x11),(0x6F2C,0x12),
            (0x6F51,0x13),(0x6F56,0x14),(0x6F7A,0x13),(0x6F7F,0x14),(0x6F86,0x12),
            (0x6F93,0x12),(0x6F9E,None),(0x6FA1,None),(0x6FA4,None),(0x6FA7,None),(0x6FAA,None))
    try:
        gb.hook_register(1,0x69FB,start,None)
        gb.hook_register(1,0x6806,end,None)
        for pc,register in writes:
            gb.hook_register(1,pc,write,register)
        for _ in range(frames):
            gb.tick()
    finally:
        gb.stop()
    return records
