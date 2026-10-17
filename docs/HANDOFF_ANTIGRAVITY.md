# Active handoff to Antigravity

The user requested that you take over development while Codex credits recover.
Work locally in the Antigravity worktree on antigravity/development, test changes,
and push coherent milestones to the specified GitHub repository. Continue doing
useful independent work with focused, budget-conscious steps.

## Overarching Project Objective

Understand and reproduce how Game Boy games work internally, using Alleyway as the
first concrete case study, to build reusable components (ROM/header parsing, deterministic
input/trace timelines, RAM differential scanners, 2bpp tile decoders, APU audio) to
accelerate future Game Boy native ports.

## Architectural Boundaries

1. Native Reimplementation (Chosen route): Native C logic faithfully executing the
   measured formulas, state machines, and fixed-point math, reading original assets
   from local ROM at runtime.
2. Emulation: Complete hardware virtualization (CPU, PPU, APU, timers). Used as reference
   oracle (PyBoy, SameBoy), not embedded in the port.
3. Static Recompilation: Automated binary translation. Not assumed or promised.

## What exists and was verified

- Stage 1 Native C playable engine in `src/main.c` (Win32/GDI at 60 Hz).
- Bit-exact trace verification (`--verify-trace`) comparing native C simulation
  against PyBoy logic ticks (`0x051F..0x0525`) for 5,651 consecutive ticks (0 mismatches).
- Deterministic input timeline, hardware register/watchpoint tracer (`tools/capture_reference.py`).
- Trace and metadata comparator (`tools/compare_traces.py`).
- Differential RAM scanner and time-series correlation finder (`tools/scan_ram.py`).
- Synthetic interactive ROM fixture generator (`tools/synthetic_rom.py`).
- Cartridge header, size, bank alignment, and checksum inspector (`tools/inspect_rom.py`).
- GitHub Actions CI workflow running on Windows with synthetic fixtures only.
- Local user dump verified at `../Local-Privado/roms/Alleyway (USA).gb`.

## Evidence vs Limits

- Verified: Stage 1 paddle movement speeds (1, 3, 5 px/tick), ball 8.8 big-endian launch
  and bounce vectors, paddle zone table lookup, wall rebounds, 4-probe brick collision,
  durability and score calculations.
- Limits: 5651 ticks of verified parity on Stage 1 do NOT prove that all game paths or all
  32 stages are complete. Moving brick scrolls, bonus stages, full 1-UP progression,
  authentic 2bpp tile decoding, and APU audio remain to be implemented.

## Roadmap & Budget Guidelines

- Maintain Gemini 3.8 Flash (Medium) as active model; avoid heavy multi-agent spawns.
- Focus on small, bounded milestones backed by synthetic unit tests:
  1. Reusable 2bpp tile decoding module + connection to local Alleyway rendering.
  2. Horizontal/vertical brick scrolling and bonus stage mechanics.
  3. Reusable Game Boy APU research, license evaluation, and first synchronized audio event.
- Update `docs/STATUS.md` and `MODLOG.md` after each verified milestone.
