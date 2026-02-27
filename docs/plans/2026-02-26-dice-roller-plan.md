# Dice Roller Implementation Plan

**Status:** COMPLETED

**Goal:** Build a native Flipper Zero dice roller app with D&D polyhedral dice (d4/d6/d8/d10/d12/d20), multi-dice rolls, animation, and a Claude co-author splash screen.

**Architecture:** Single-file C app using ViewPort + FuriMessageQueue event loop. State machine with 4 states: Splash → Main → Rolling → Result. FuriTimer drives roll animation. XBM sprite for Claude pixel art on splash screen.

**Tech Stack:** C, Flipper Zero Firmware SDK (furi, gui, input, furi_hal_random, notification)

**Target Firmware:** Momentum (mntm-012)

---

## Tasks (all completed)

### Task 1: Create project scaffold ✅
- Created `application.fam` and minimal `dice_roller.c`

### Task 2: Create Claude XBM sprite and menu icon ✅
- 24x32px Claude mascot pixel art (inline XBM in C)
- 10x10px 1-bit dice menu icon PNG

### Task 3: Implement app state and event loop ✅
- DiceState struct, DiceScreen enum, DiceEvent
- FuriMessageQueue + FuriTimer event loop
- Input handling for all states

### Task 4: Implement splash screen drawing ✅
- Claude XBM bitmap centered
- App title and co-author credit

### Task 5: Implement main/result screen drawing ✅
- Dice type selector (< d6 >)
- Quantity selector (- Qty: N +)
- Single die: FontBigNumbers centered
- Multiple dice: grid layout with rounded frames + sum
- Roll animation: 10 frames at 50ms

### Task 6: Refine Claude XBM sprite ✅
- Detailed pixel art: flower, hat, rounded body, eyes, legs

### Task 7: Create 10x10 menu icon ✅
- 1-bit PNG with d6 face (6 pips)

### Task 8: Final polish ✅
- Haptic feedback (vibration on roll completion)
- NotificationApp integration
- Proper viewport cleanup

## Build & Deploy

```bash
cd dice_roller

# First time: set SDK to Momentum firmware
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json

# Build
ufbt

# Deploy via USB (Flipper must be unlocked)
ufbt launch
```
