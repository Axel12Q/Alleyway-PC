# Active handoff to Antigravity

The user requested that you take over development while Codex credits recover.
Work locally in the Antigravity worktree on antigravity/development, test changes,
and push coherent milestones to the specified GitHub repository. Continue doing
useful independent work without asking routine implementation questions.

## What exists and was verified

- Native Windows C diagnostic renderer (Win32/GDI), 160x144 integer scaling.
- Python ROM metadata/checksum inspector with two synthetic tests.
- PyBoy no-input boot capture to PPM/JSON, repeatable on a synthetic cartridge.
- SameBoy 1.0.4 portable download, publisher SHA256 verified.
- PyBoy 2.8.1, Zig 0.16.0 and dependency lock.
- No original Alleyway dump, gameplay, RAM map or extracted levels have been validated.

## Start here

1. Read AGENTS.md, README.md, docs/STATUS.md, MODLOG.md and RESEARCH_PROTOCOL.md.
2. Check current branch, git status and origin. Remain in this worktree.
3. Use .venv/Scripts/python.exe explicitly. If git/uv is missing from terminal PATH,
   dot-source .tools/machine-env.ps1; it adds existing tools only for that shell.
4. Run tools/build.py, build/alleyway-lab.exe --smoke-test and unittest discovery.
5. Check only this worktree's local/roms and ../Local-Privado/roms for a user-provided
   genuine dump. synthetic.gb is a synthetic fixture, NEVER Alleyway evidence.

## Useful work that does not require the original ROM

- Harden and test metadata errors, reference capture/replay and a trace comparator.
- Add a deterministic input timeline (frame, button, press/release), explicit emulator
  settings and version/hash metadata. Keep captures private and schema documented.
- Compare repeat runs on synthetic fixtures, including a deliberate mismatch test.
- Prepare debugger workflow and RAM discovery experiments from official documentation.
- Improve Windows setup/build usability and a small CI workflow using synthetic data.
- Keep actual game logic unspecified until measured. Do not make a Breakout clone
  and describe it as a faithful Alleyway port.

Choose concrete small milestones; implement, test, document, commit and push each.
Do not endlessly expand scaffolding if all meaningful work needs the ROM.

## After the user supplies the original dump

Identify hash/revision, reproduce boot/controls, locate actual state variables,
measure movement/rebounds/collision timing, decode one level locally, implement
one native playable stage, compare recorded traces, then extend levels/audio/menus.
The port should read/import the user's data locally; no original resources in Git.
Known controls/serial-port pitfall:
https://github.com/visualboyadvance-m/visualboyadvance-m/issues/274

## Git and progress

Commit author: Antigravity (use existing user identity if intentionally configured;
otherwise per-command `-c user.name=Antigravity -c user.email=antigravity@localhost`).
Push antigravity/development after successful milestones. No force push or main
merge. On a genuine blocker update STATUS with what is done, exact missing input
and next command; tell the user. Never claim the complete game was ported if not.
