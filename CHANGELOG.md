# Changelog

## 0.1.0-preview

- First portable Windows x64 preview; reads the user's local supported USA ROM.
- ROM picker, supported-ROM identity check and exact file-size validation.
- Short keyboard taps survive the message-pump/simulation boundary.
- Changing windows suspends play, clears held inputs and flushes queued sound.
- Pause, reset and title transitions silence pending output; M toggles mute.
- Channel-1 software SFX sequencer replaces endless static wall/ceiling tones.
- Primary SFX 1..12 checked against the original driver's state/register writes.
- Existing logic, 32 curtain transitions, timing, visual snapshots and window tests retained.
- Reproducible allowlisted release package, SHA-256 manifest and original source license.

This preview still lacks channels 2..4/full music and complete intermediate animations.
It is a native reimplementation, not an automated recompiler or a complete CPU emulator.
