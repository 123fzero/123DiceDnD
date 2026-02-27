# 3D Dice Animation — Design

## Summary

Add pseudo-3D rotating dice animation to the Dice Roller app. Pre-rendered XBM sprite frames for all 6 polyhedral types (d4, d6, d8, d10, d12, d20), with decelerating rotation and phase-shifted multi-dice display.

## Sprites

6 dice types × 8 rotation frames = 48 sprites, each 24×24px (72 bytes). ~3.5KB total.

| Type | Shape | Frames |
|------|-------|--------|
| d4 | Tetrahedron | 8 × 24×24px |
| d6 | Cube | 8 × 24×24px |
| d8 | Octahedron | 8 × 24×24px |
| d10 | Pentagonal trapezohedron | 8 × 24×24px |
| d12 | Dodecahedron | 8 × 24×24px |
| d20 | Icosahedron | 8 × 24×24px |

## Sprite Generation

Python script renders 3D wireframe+fill of each polyhedron, rotates in 45° increments, rasterizes to 24×24 1-bit, exports as C XBM arrays.

## Animation Timing

Decelerating: starts fast, slows down to stop on result.

```
Frame:    0    1    2    3    4    5    6    7    ...  final
Delay:   50ms 50ms 50ms 50ms 80ms 80ms 120ms 150ms  stop
```

Total ~12-15 frames, ~1 second.

## Multi-dice

- Each die animates with phase offset (stagger by 2 frames)
- Prevents all dice spinning in sync
- Layout: grid of 24×24 sprites (same grid as current)

## Single die

- Larger sprite (~40×40px) centered on screen
- Separate "large" sprite set, or scale up the 24×24 (double pixels)

## Files

- `dice_roller/dice_sprites.h` — all XBM arrays
- `dice_roller/tools/gen_sprites.py` — generation script
- Modified `dice_roller/dice_roller.c` — animation logic

## Changes to dice_roller.c

- New animation state with per-die frame counters and phase offsets
- Variable timer interval for deceleration
- Draw sprites instead of text during Rolling state
- Result screen: show final sprite frame with number overlay
