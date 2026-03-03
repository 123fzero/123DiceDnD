# 123DiceDnD for Flipper Zero — Design

## Summary

Native Flipper Zero app: D&D dice roller with full polyhedral set (d4, d6, d8, d10, d12, d20), multiple dice (1-6), roll animation, haptic feedback, and Claude co-author splash screen.

## Screens

### Splash (~2 sec)
- Pixel art Claude character (24x32px, 1-bit XBM inline in C code)
- "123DiceDnD" + "Co-authored by Claude"
- Auto-dismiss after 2 seconds, or skip with any button

### Main / Result
- Single screen, no menus
- Left/Right: cycle dice type (d4 → d6 → d8 → d10 → d12 → d20)
- Up/Down: adjust quantity (1–6)
- OK: roll dice
- Back: exit app
- Single die: large centered number (FontBigNumbers)
- Multiple dice: each result in rounded frame, grid layout (3 cols max), sum at bottom

### Roll Animation
- ~500ms rapid random number cycling in result positions
- Timer-driven, ~50ms intervals (10 frames)
- Vibration feedback on completion

## Architecture

- State machine: Splash → Main → Rolling → Result
- Single ViewPort with draw/input callbacks
- `furi_hal_random_get()` for hardware RNG
- FuriTimer for animation ticks (20Hz)
- NotificationApp for haptic feedback
- FuriMutex for thread-safe state access

## File Structure

```
dice_roller/
├── application.fam          # App manifest (Games, gui+notification)
├── dice_roller.c            # All app logic (~413 lines)
└── images/
    └── dice_10px.png        # 10x10 1-bit menu icon
```

## Controls

| Button | Action |
|--------|--------|
| Left/Right | Switch dice type |
| Up/Down | Change quantity (1-6) |
| OK | Roll dice |
| Back (short) | Exit app |
| Back (long) | Force exit |

## Build

Requires Momentum firmware SDK (matching device firmware):
```bash
cd dice_roller
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json
ufbt          # build
ufbt launch   # deploy + run via USB
```
