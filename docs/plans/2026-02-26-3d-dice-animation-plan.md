# 3D Dice Animation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add pseudo-3D rotating sprite animation for all 6 polyhedral dice types to the existing Flipper Zero Dice Roller app.

**Architecture:** Python script generates 3D→2D projected wireframe+fill renders of each polyhedron at 8 rotation angles, rasterized to 24×24 1-bit XBM. Exported as a C header. The C code references these sprites during the Rolling state with decelerating frame timing and per-die phase offsets.

**Tech Stack:** Python 3 + numpy + Pillow (sprite generation), C / Flipper Zero SDK (runtime)

---

### Task 1: Create sprite generation script — 3D math core

**Files:**
- Create: `dice_roller/tools/gen_sprites.py`

**Step 1: Create the script with polyhedron vertex data and 3D projection**

```python
#!/usr/bin/env python3
"""Generate 24x24 1-bit XBM sprite frames for all D&D polyhedral dice."""
import numpy as np
from PIL import Image, ImageDraw
import math

SPRITE_SIZE = 24
NUM_FRAMES = 8

# --- Polyhedron vertex definitions (unit-normalized) ---

def make_tetrahedron():
    """d4: 4 vertices, 4 triangular faces."""
    v = np.array([
        [ 1,  1,  1],
        [ 1, -1, -1],
        [-1,  1, -1],
        [-1, -1,  1],
    ], dtype=float)
    v /= np.linalg.norm(v[0])
    faces = [(0,1,2), (0,1,3), (0,2,3), (1,2,3)]
    return v, faces

def make_cube():
    """d6: 8 vertices, 6 quad faces."""
    v = np.array([
        [-1,-1,-1], [ 1,-1,-1], [ 1, 1,-1], [-1, 1,-1],
        [-1,-1, 1], [ 1,-1, 1], [ 1, 1, 1], [-1, 1, 1],
    ], dtype=float)
    faces = [
        (0,1,2,3), (4,5,6,7),
        (0,1,5,4), (2,3,7,6),
        (0,3,7,4), (1,2,6,5),
    ]
    return v, faces

def make_octahedron():
    """d8: 6 vertices, 8 triangular faces."""
    v = np.array([
        [ 0, 0, 1], [ 0, 0,-1],
        [ 1, 0, 0], [-1, 0, 0],
        [ 0, 1, 0], [ 0,-1, 0],
    ], dtype=float)
    faces = [
        (0,2,4), (0,4,3), (0,3,5), (0,5,2),
        (1,2,4), (1,4,3), (1,3,5), (1,5,2),
    ]
    return v, faces

def make_pentagonal_trapezohedron():
    """d10: 10 kite faces. Approximate with two pentagons + 2 poles."""
    verts = []
    # Top cap vertices (5 points, slightly above center)
    for i in range(5):
        angle = 2 * math.pi * i / 5
        verts.append([math.cos(angle), math.sin(angle), 0.4])
    # Bottom cap vertices (5 points, rotated 36 deg, slightly below)
    for i in range(5):
        angle = 2 * math.pi * i / 5 + math.pi / 5
        verts.append([math.cos(angle), math.sin(angle), -0.4])
    # Top and bottom poles
    verts.append([0, 0, 1.2])   # index 10
    verts.append([0, 0, -1.2])  # index 11
    v = np.array(verts, dtype=float)
    faces = []
    for i in range(5):
        # Upper kite: pole-top, top[i], bottom[i], top[i+1]
        faces.append((10, i, 5 + i))
        faces.append((10, (i+1)%5, 5 + i))
        # Lower kite
        faces.append((11, 5 + i, (i+1)%5))
        faces.append((11, 5 + (i+1)%5, (i+1)%5))
    return v, faces

def make_dodecahedron():
    """d12: 20 vertices, 12 pentagonal faces."""
    phi = (1 + math.sqrt(5)) / 2
    verts = []
    # Cube vertices
    for s1 in [-1, 1]:
        for s2 in [-1, 1]:
            for s3 in [-1, 1]:
                verts.append([s1, s2, s3])
    # Rectangle vertices
    for s1 in [-1, 1]:
        for s2 in [-1, 1]:
            verts.append([0, s1/phi, s2*phi])
            verts.append([s1/phi, s2*phi, 0])
            verts.append([s2*phi, 0, s1/phi])
    v = np.array(verts, dtype=float)
    # Approximate faces by connecting nearby vertices
    # Use convex hull approach
    from scipy.spatial import ConvexHull
    hull = ConvexHull(v)
    faces = [tuple(f) for f in hull.simplices]
    return v, faces

def make_icosahedron():
    """d20: 12 vertices, 20 triangular faces."""
    phi = (1 + math.sqrt(5)) / 2
    v = np.array([
        [-1,  phi, 0], [ 1,  phi, 0], [-1, -phi, 0], [ 1, -phi, 0],
        [0, -1,  phi], [0,  1,  phi], [0, -1, -phi], [0,  1, -phi],
        [ phi, 0, -1], [ phi, 0,  1], [-phi, 0, -1], [-phi, 0,  1],
    ], dtype=float)
    v /= np.linalg.norm(v[0])
    faces = [
        (0,11,5),  (0,5,1),  (0,1,7),  (0,7,10), (0,10,11),
        (1,5,9),   (5,11,4), (11,10,2), (10,7,6), (7,1,8),
        (3,9,4),   (3,4,2),  (3,2,6),   (3,6,8),  (3,8,9),
        (4,9,5),   (2,4,11), (6,2,10),  (8,6,7),  (9,8,1),
    ]
    return v, faces


# --- 3D Rotation ---

def rotation_matrix(angle_x, angle_y, angle_z):
    """Create combined rotation matrix."""
    cx, sx = math.cos(angle_x), math.sin(angle_x)
    cy, sy = math.cos(angle_y), math.sin(angle_y)
    cz, sz = math.cos(angle_z), math.sin(angle_z)
    Rx = np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]])
    Ry = np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])
    Rz = np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])
    return Rz @ Ry @ Rx


# --- Projection & Rendering ---

def project(vertices, angle_y, tilt=0.4):
    """Rotate and project 3D vertices to 2D."""
    R = rotation_matrix(tilt, angle_y, 0)
    rotated = (R @ vertices.T).T
    # Simple orthographic projection
    scale = SPRITE_SIZE * 0.35
    cx, cy = SPRITE_SIZE / 2, SPRITE_SIZE / 2
    pts_2d = []
    for v in rotated:
        pts_2d.append((cx + v[0] * scale, cy - v[1] * scale))
    return pts_2d, rotated


def face_visible(face_verts_3d):
    """Check if face is front-facing (z-component of normal > 0)."""
    if len(face_verts_3d) < 3:
        return False
    v0, v1, v2 = face_verts_3d[0], face_verts_3d[1], face_verts_3d[2]
    # Cross product of two edges
    edge1 = v1 - v0
    edge2 = v2 - v0
    normal_z = edge1[0] * edge2[1] - edge1[1] * edge2[0]
    return normal_z > 0


def render_frame(vertices, faces, angle_y, tilt=0.4):
    """Render one frame as a 24x24 1-bit image."""
    img = Image.new('1', (SPRITE_SIZE, SPRITE_SIZE), 0)
    draw = ImageDraw.Draw(img)

    pts_2d, rotated = project(vertices, angle_y, tilt)

    # Sort faces by average Z depth (painter's algorithm)
    face_depths = []
    for face in faces:
        face_verts_3d = np.array([rotated[i] for i in face])
        avg_z = np.mean(face_verts_3d[:, 2])
        face_depths.append((avg_z, face, face_verts_3d))
    face_depths.sort(key=lambda x: x[0])  # back to front

    for _, face, face_verts_3d in face_depths:
        if not face_visible(face_verts_3d):
            continue
        poly = [pts_2d[i] for i in face]
        if len(poly) >= 3:
            # Fill face white, outline black
            draw.polygon(poly, fill=1, outline=1)

    # Draw edges on top for all visible faces
    for _, face, face_verts_3d in face_depths:
        if not face_visible(face_verts_3d):
            continue
        poly = [pts_2d[i] for i in face]
        for i in range(len(poly)):
            p1 = poly[i]
            p2 = poly[(i + 1) % len(poly)]
            draw.line([p1, p2], fill=0, width=1)

    return img


# --- XBM Export ---

def image_to_xbm_bytes(img):
    """Convert 24x24 1-bit image to XBM byte array (LSB-first)."""
    pixels = img.load()
    w, h = img.size
    bytes_per_row = (w + 7) // 8
    data = []
    for y in range(h):
        for bx in range(bytes_per_row):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and pixels[x, y]:
                    byte |= (1 << bit)
            data.append(byte)
    return data


def generate_all():
    """Generate all sprites and output as C header."""
    polyhedra = {
        'd4':  make_tetrahedron,
        'd6':  make_cube,
        'd8':  make_octahedron,
        'd10': make_pentagonal_trapezohedron,
        'd12': make_dodecahedron,
        'd20': make_icosahedron,
    }

    lines = []
    lines.append('/* Auto-generated 3D dice sprites - do not edit manually */')
    lines.append('#pragma once')
    lines.append('')
    lines.append(f'#define DICE_SPRITE_SIZE {SPRITE_SIZE}')
    lines.append(f'#define DICE_SPRITE_FRAMES {NUM_FRAMES}')
    lines.append(f'#define DICE_SPRITE_BYTES ({SPRITE_SIZE} * {SPRITE_SIZE} / 8)')
    lines.append('')

    type_order = ['d4', 'd6', 'd8', 'd10', 'd12', 'd20']

    for dtype in type_order:
        make_fn = polyhedra[dtype]
        try:
            vertices, faces = make_fn()
        except ImportError:
            # scipy not available for dodecahedron, use simplified version
            continue

        lines.append(f'/* {dtype} rotation frames */')
        lines.append(f'static const uint8_t sprite_{dtype}[{NUM_FRAMES}][{SPRITE_SIZE * SPRITE_SIZE // 8}] = {{')

        for frame in range(NUM_FRAMES):
            angle = 2 * math.pi * frame / NUM_FRAMES
            img = render_frame(vertices, faces, angle)
            xbm = image_to_xbm_bytes(img)
            hex_str = ', '.join(f'0x{b:02X}' for b in xbm)
            comma = ',' if frame < NUM_FRAMES - 1 else ''
            lines.append(f'    {{ {hex_str} }}{comma}')

        lines.append('};')
        lines.append('')

    # Lookup table
    lines.append('/* Sprite lookup by dice type (matches DiceType enum order) */')
    lines.append(f'static const uint8_t* const dice_sprites[6][{NUM_FRAMES}] = {{')
    for dtype in type_order:
        entries = ', '.join(f'sprite_{dtype}[{i}]' for i in range(NUM_FRAMES))
        lines.append(f'    {{ {entries} }},  /* {dtype} */')
    lines.append('};')

    return '\n'.join(lines)


if __name__ == '__main__':
    header = generate_all()
    with open('dice_sprites.h', 'w') as f:
        f.write(header + '\n')
    print(f'Generated dice_sprites.h')
```

**Step 2: Commit**

```bash
git add dice_roller/tools/gen_sprites.py
git commit -m "feat: add 3D dice sprite generation script"
```

---

### Task 2: Run sprite generation and fix d12

The d12 (dodecahedron) uses scipy ConvexHull which may not be available. Replace with hardcoded faces. Then run the script.

**Files:**
- Modify: `dice_roller/tools/gen_sprites.py` (fix d12 if scipy fails)
- Create: `dice_roller/dice_sprites.h` (generated output)

**Step 1: Try running the script**

```bash
cd dice_roller && python3 tools/gen_sprites.py
```

If it fails due to scipy, replace `make_dodecahedron()` with hardcoded triangulated faces (the dodecahedron has known face indices). Then rerun.

If numpy is missing: `pip3 install numpy Pillow`

**Step 2: Verify the output**

```bash
head -20 dice_roller/dice_sprites.h
wc -l dice_roller/dice_sprites.h
```

Should see ~400-500 lines with XBM arrays for all 6 dice types.

**Step 3: Commit**

```bash
git add dice_roller/dice_sprites.h dice_roller/tools/gen_sprites.py
git commit -m "feat: generate 3D dice XBM sprites for all 6 types"
```

---

### Task 3: Add animation state to DiceState

**Files:**
- Modify: `dice_roller/dice_roller.c`

**Step 1: Add `#include "dice_sprites.h"` after existing includes**

At line 6, after `#include <notification/notification_messages.h>`, add:
```c
#include "dice_sprites.h"
```

**Step 2: Update DiceState struct and animation constants**

Replace:
```c
#define ANIMATION_FRAMES 10
```
With:
```c
#define TOTAL_ANIM_TICKS 16
```

Add to DiceState:
```c
typedef struct {
    DiceScreen screen;
    DiceType dice_type;
    int quantity;
    int results[MAX_DICE];
    int sum;
    int anim_tick;                    /* global animation tick counter */
    int die_phase[MAX_DICE];          /* per-die phase offset (0-7) */
    FuriMutex* mutex;
} DiceState;
```

**Step 3: Add deceleration timing lookup**

```c
/* Tick thresholds for deceleration: frame advances at these ticks */
/* Fast at start, slowing down toward end */
static const int anim_schedule[] = {
    0, 1, 2, 3, 4,    /* every tick (fast) */
    6, 8,              /* every 2 ticks */
    11, 15,            /* every 3-4 ticks */
};
#define ANIM_SCHEDULE_LEN 9

/* Get sprite frame index (0-7) for a given animation tick + phase offset */
static int get_sprite_frame(int tick, int phase) {
    int frame = 0;
    for(int i = 0; i < ANIM_SCHEDULE_LEN; i++) {
        if(tick >= anim_schedule[i]) frame = i;
    }
    return (frame + phase) % DICE_SPRITE_FRAMES;
}
```

**Step 4: Commit**

```bash
git add dice_roller/dice_roller.c
git commit -m "feat: add 3D animation state and deceleration timing"
```

---

### Task 4: Update rolling logic in event loop

**Files:**
- Modify: `dice_roller/dice_roller.c`

**Step 1: Update the OK press handler to initialize phase offsets**

Replace the InputKeyOk case:
```c
case InputKeyOk:
    /* Start rolling with random phase offsets */
    state->anim_tick = 0;
    for(int i = 0; i < state->quantity; i++) {
        state->die_phase[i] = (int)(furi_hal_random_get() % DICE_SPRITE_FRAMES);
    }
    state->screen = DiceScreenRolling;
    break;
```

**Step 2: Update the Rolling tick handler**

Replace the `case DiceScreenRolling:` block:
```c
case DiceScreenRolling:
    state->anim_tick++;
    /* Generate random intermediate values each tick */
    roll_all_dice(state);
    if(state->anim_tick >= TOTAL_ANIM_TICKS) {
        /* Final roll */
        roll_all_dice(state);
        state->screen = DiceScreenResult;
        notification_message(notification, &sequence_single_vibro);
    }
    break;
```

**Step 3: Initialize die_phase in state allocation**

In `dice_roller_main`, after `state->results[i] = 0;` loop, add:
```c
for(int i = 0; i < MAX_DICE; i++) {
    state->die_phase[i] = 0;
}
```

**Step 4: Commit**

```bash
git add dice_roller/dice_roller.c
git commit -m "feat: update rolling logic with phase offsets and deceleration"
```

---

### Task 5: Update draw function to render 3D sprites

**Files:**
- Modify: `dice_roller/dice_roller.c`

**Step 1: Add sprite drawing helper**

```c
/* Draw a die sprite at position, optionally 2x scaled */
static void draw_die_sprite(Canvas* canvas, int x, int y, DiceType type, int frame, bool large) {
    const uint8_t* sprite = dice_sprites[type][frame % DICE_SPRITE_FRAMES];
    if(large) {
        /* 2x pixel doubling for single die: 24→48px */
        for(int sy = 0; sy < DICE_SPRITE_SIZE; sy++) {
            for(int sx = 0; sx < DICE_SPRITE_SIZE; sx++) {
                int byte_idx = sy * (DICE_SPRITE_SIZE / 8) + sx / 8;
                int bit_idx = sx % 8;
                if(sprite[byte_idx] & (1 << bit_idx)) {
                    canvas_draw_box(canvas, x + sx * 2, y + sy * 2, 2, 2);
                }
            }
        }
    } else {
        canvas_draw_xbm(canvas, x, y, DICE_SPRITE_SIZE, DICE_SPRITE_SIZE, sprite);
    }
}
```

**Step 2: Replace the Rolling draw code in draw_main()**

In the `else` block of `draw_main()` (where `state->screen != DiceScreenMain`), when `state->screen == DiceScreenRolling`, draw sprites instead of number text:

Replace the entire `else` block (lines 123-192) with:

```c
    } else if(state->screen == DiceScreenRolling) {
        /* Rolling: show 3D rotating sprites */
        if(state->quantity == 1) {
            /* Single die: large centered sprite (2x = 48x48) */
            int frame = get_sprite_frame(state->anim_tick, state->die_phase[0]);
            draw_die_sprite(canvas, 40, 16, state->dice_type, frame, true);
        } else {
            /* Multiple dice: small sprites in grid */
            int cols = 3;
            if(state->quantity <= 2) cols = 2;

            int cell_w = 32;
            int cell_h = 28;
            int grid_w = cols * cell_w;
            int rows = (state->quantity + cols - 1) / cols;
            int grid_h = rows * cell_h;

            int area_top = 16;
            int area_h = 48;
            int start_y = area_top + (area_h - grid_h) / 2;
            int start_x = (128 - grid_w) / 2;

            for(int i = 0; i < state->quantity; i++) {
                int col = i % cols;
                int row = i / cols;
                int cx = start_x + col * cell_w + (cell_w - DICE_SPRITE_SIZE) / 2;
                int cy = start_y + row * cell_h + (cell_h - DICE_SPRITE_SIZE) / 2;
                int frame = get_sprite_frame(state->anim_tick, state->die_phase[i]);
                draw_die_sprite(canvas, cx, cy, state->dice_type, frame, false);
            }
        }
    } else {
        /* Result: show final values (existing code) */
```

Keep the existing Result display code (numbers in rounded frames, sum, etc.) unchanged after this.

**Step 3: Commit**

```bash
git add dice_roller/dice_roller.c
git commit -m "feat: render 3D rotating sprites during dice roll animation"
```

---

### Task 6: Build, test, and deploy

**Files:**
- No new files

**Step 1: Build**

```bash
cd /Users/colinfrl/work/fzapp/dice_roller
ufbt 2>&1 | tail -10
```

Expected: clean compilation, `dice_roller.fap` produced.

**Step 2: Fix any compile errors**

Common issues:
- Missing DICE_SPRITE_BYTES definition mismatch
- Array size mismatches
- `anim_frame` references that should be `anim_tick`

**Step 3: Deploy and test**

```bash
ufbt launch
```

Test on Flipper:
- Select each dice type (d4 through d20), press OK
- Verify 3D sprite rotates and decelerates
- Verify single die shows large sprite
- Verify multiple dice show small sprites with staggered animation
- Verify result screen still shows numbers correctly

**Step 4: Commit any fixes**

```bash
git add dice_roller/
git commit -m "fix: resolve build issues for 3D dice animation"
```

---

### Task 7: Final polish

**Files:**
- Modify: `dice_roller/dice_roller.c`

**Step 1: Verify result screen still works properly**

After animation ends, the Result screen should show the existing number display (big number for single die, grid of numbers with sum for multiple). No sprite on the Result screen — just the clean number result.

**Step 2: Clean up any unused code**

Remove old `ANIMATION_FRAMES` if still present. Ensure no dead code from the old text-only animation.

**Step 3: Build and deploy final version**

```bash
ufbt && ufbt launch
```

**Step 4: Commit**

```bash
git add dice_roller/
git commit -m "feat: finalize 3D dice animation, clean up"
```
