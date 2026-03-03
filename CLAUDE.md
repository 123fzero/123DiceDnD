# CLAUDE.md

## Project

Flipper Zero native app — `123DiceDnD`. Runs on Momentum firmware.

## Build

```bash
cd dice_roller
ufbt          # build
ufbt launch   # build + deploy + run via USB
```

SDK: Momentum firmware (`ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json`)

## Key Files

- `dice_roller/dice_roller.c` — all app logic (state machine, drawing, input, animation)
- `dice_roller/dice_sprites.h` — auto-generated XBM sprite arrays (do not edit manually)
- `dice_roller/tools/gen_sprites.py` — Python script to regenerate sprites
- `dice_roller/application.fam` — app manifest (appid: `dice_roller`, name: `123DiceDnD`)

## Architecture

- Single-file C app, ViewPort + FuriMessageQueue event loop
- State machine: Splash → Main → Rolling → Result
- 3D sprites: 6 dice types × 8 rotation frames, 24×24px XBM
- Sprite lookup via `dice_sprite_get()` inline function (not pointer table — avoids FAP relocation bugs)
- Timer: FuriTimer at 50ms (20Hz) drives animation and splash timeout

## Conventions

- Target firmware: Momentum (not official Flipper)
- Local clang errors for `Canvas`, `furi.h` etc are false positives — SDK headers only in Flipper toolchain
- `ufbt launch` deploys to `/ext/apps/Games/dice_roller.fap` — watch for stale FAP files with old names on SD card
- Serial debugging: `log debug` via CLI on `/dev/cu.usbmodemflip_Rank3r31`
- Stack size: 8KB, app requires `gui` and `notification` services

## Versioning

- Semver (MAJOR.MINOR.PATCH) in `dice_roller/application.fam` field `fap_version`
- Every commit bumps version and gets an annotated git tag `vX.Y.Z`
- Bug fixes → PATCH, new features → MINOR, breaking changes → MAJOR

## Language

User prefers communication in Russian. All code and documentation must be in English.
