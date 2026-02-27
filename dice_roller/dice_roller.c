#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <notification/notification_messages.h>
#include "dice_sprites.h"

/* Classic Claude mascot - rounded blob with sparkle, no hat */
#define CLAUDE_WIDTH 24
#define CLAUDE_HEIGHT 32
static const uint8_t claude_xbm[] = {
    0x00, 0x00, 0x00,  /* Row  0: empty                     */
    0x00, 0x08, 0x00,  /* Row  1: sparkle top dot           */
    0x00, 0x14, 0x00,  /* Row  2: sparkle arms              */
    0x00, 0x08, 0x00,  /* Row  3: sparkle bottom dot        */
    0x00, 0x00, 0x00,  /* Row  4: space                     */
    0x00, 0x7E, 0x00,  /* Row  5: head top (round)          */
    0x80, 0xFF, 0x01,  /* Row  6: head widening             */
    0xC0, 0xFF, 0x03,  /* Row  7: head                      */
    0xE0, 0xFF, 0x07,  /* Row  8: head wider                */
    0xF0, 0xFF, 0x0F,  /* Row  9: body top                  */
    0xF0, 0xFF, 0x0F,  /* Row 10: body                      */
    0xF8, 0xFF, 0x1F,  /* Row 11: body full width           */
    0xF8, 0xFF, 0x1F,  /* Row 12: body                      */
    0xF8, 0xFF, 0x1F,  /* Row 13: body                      */
    0xB8, 0xFF, 0x1D,  /* Row 14: eyes (2x2 gaps)           */
    0xB8, 0xFF, 0x1D,  /* Row 15: eyes                      */
    0xF8, 0xFF, 0x1F,  /* Row 16: body below eyes           */
    0xF8, 0xFF, 0x1F,  /* Row 17: body                      */
    0xF8, 0xFF, 0x1F,  /* Row 18: body                      */
    0xF8, 0xFF, 0x1F,  /* Row 19: body                      */
    0xF8, 0xFF, 0x1F,  /* Row 20: body                      */
    0xF0, 0xFF, 0x0F,  /* Row 21: narrowing                 */
    0xF0, 0xFF, 0x0F,  /* Row 22: narrowing                 */
    0xE0, 0xFF, 0x07,  /* Row 23: narrowing                 */
    0xC0, 0xFF, 0x03,  /* Row 24: narrow                    */
    0xC0, 0xFF, 0x03,  /* Row 25: waist                     */
    0xC0, 0xC3, 0x03,  /* Row 26: leg split                 */
    0xE0, 0xC3, 0x07,  /* Row 27: legs with feet            */
    0xE0, 0x81, 0x07,  /* Row 28: feet spread               */
    0x00, 0x00, 0x00,  /* Row 29: empty                     */
    0x00, 0x00, 0x00,  /* Row 30: empty                     */
    0x00, 0x00, 0x00,  /* Row 31: empty                     */
};

/* Screen states */
typedef enum {
    DiceScreenSplash,
    DiceScreenMain,
    DiceScreenRolling,
    DiceScreenResult,
} DiceScreen;

/* Event types */
typedef enum {
    EventTypeTick,
    EventTypeInput,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} DiceEvent;

/* Dice types */
typedef enum {
    DiceTypeD4 = 0,
    DiceTypeD6,
    DiceTypeD8,
    DiceTypeD10,
    DiceTypeD12,
    DiceTypeD20,
    DiceTypeCount,
} DiceType;

static const int dice_sides[] = {4, 6, 8, 10, 12, 20};
static const char* dice_names[] = {"d4", "d6", "d8", "d10", "d12", "d20"};

#define MAX_DICE 6
#define TOTAL_ANIM_TICKS 16
#define SPLASH_DURATION_MS 4000

typedef struct {
    DiceScreen screen;
    DiceType dice_type;
    int quantity;
    int results[MAX_DICE];
    int sum;
    int anim_tick;               /* global animation tick counter */
    int die_phase[MAX_DICE];     /* per-die sprite phase offset (0-7) */
    FuriMutex* mutex;
} DiceState;

/* Tick thresholds: sprite frame advances at these animation ticks */
static const int anim_schedule[] = {0, 1, 2, 3, 4, 6, 8, 11, 15};
#define ANIM_SCHEDULE_LEN 9

/* Map animation tick + phase offset to sprite frame index (0-7) */
static int get_sprite_frame(int tick, int phase) {
    int frame = 0;
    for(int i = 0; i < ANIM_SCHEDULE_LEN; i++) {
        if(tick >= anim_schedule[i]) frame = i;
    }
    return (frame + phase) % DICE_SPRITE_FRAMES;
}

/* Scale a 24x24 sprite to 48x48 into a static buffer */
#define LARGE_SPRITE_SIZE 48
#define LARGE_SPRITE_BYTES (LARGE_SPRITE_SIZE / 8 * LARGE_SPRITE_SIZE) /* 288 bytes */
static uint8_t large_sprite_buf[LARGE_SPRITE_BYTES];

static void scale_sprite_2x(const uint8_t* src) {
    memset(large_sprite_buf, 0, LARGE_SPRITE_BYTES);
    for(int sy = 0; sy < DICE_SPRITE_SIZE; sy++) {
        for(int sx = 0; sx < DICE_SPRITE_SIZE; sx++) {
            int src_byte = sy * (DICE_SPRITE_SIZE / 8) + sx / 8;
            int src_bit = sx % 8;
            if(src[src_byte] & (1 << src_bit)) {
                /* Set 4 pixels in the 2x buffer (2x2 block) */
                int dx = sx * 2;
                int dy = sy * 2;
                for(int py = 0; py < 2; py++) {
                    for(int px = 0; px < 2; px++) {
                        int bx = dx + px;
                        int by = dy + py;
                        int dst_byte = by * (LARGE_SPRITE_SIZE / 8) + bx / 8;
                        int dst_bit = bx % 8;
                        large_sprite_buf[dst_byte] |= (1 << dst_bit);
                    }
                }
            }
        }
    }
}

/* Draw a die sprite, optionally 2x scaled for single-die view */
static void draw_die_sprite(Canvas* canvas, int x, int y, int dice_type, int frame, bool large) {
    const uint8_t* sprite = dice_sprite_get(dice_type, frame % DICE_SPRITE_FRAMES);
    if(large) {
        scale_sprite_2x(sprite);
        canvas_draw_xbm(canvas, x, y, LARGE_SPRITE_SIZE, LARGE_SPRITE_SIZE, large_sprite_buf);
    } else {
        canvas_draw_xbm(canvas, x, y, DICE_SPRITE_SIZE, DICE_SPRITE_SIZE, sprite);
    }
}

/* Draw the splash screen */
static void draw_splash(Canvas* canvas) {
    canvas_draw_xbm(canvas, 52, 2, CLAUDE_WIDTH, CLAUDE_HEIGHT, claude_xbm);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "Dice Roller");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignTop, "Co-authored by Claude");
}

/* Draw the main/rolling/result screen */
static void draw_main(Canvas* canvas, DiceState* state) {
    /* Header: dice type selector on left, quantity on right */
    canvas_set_font(canvas, FontSecondary);

    /* Left side: "< d6 >" dice type selector */
    char dice_label[16];
    snprintf(dice_label, sizeof(dice_label), "< %s >", dice_names[state->dice_type]);
    canvas_draw_str(canvas, 2, 10, dice_label);

    /* Right side: quantity with +/- hint */
    char qty_label[16];
    snprintf(qty_label, sizeof(qty_label), "-  Qty: %d  +", state->quantity);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, qty_label);

    /* Separator line */
    canvas_draw_line(canvas, 0, 14, 127, 14);

    if(state->screen == DiceScreenMain) {
        /* Prompt to roll */
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Press OK to roll!");
    } else if(state->screen == DiceScreenRolling) {
        /* Rolling: show 3D rotating sprites */
        if(state->quantity == 1) {
            /* Single die: large centered sprite (2x = 48x48) */
            int frame = get_sprite_frame(state->anim_tick, state->die_phase[0]);
            draw_die_sprite(canvas, 40, 16, state->dice_type, frame, true);
        } else {
            /* Multiple dice: small sprites in grid */
            int cols = (state->quantity <= 2) ? 2 : 3;
            int cell_w = 32;
            int cell_h = 28;
            int grid_w = cols * cell_w;
            int nrows = (state->quantity + cols - 1) / cols;
            int grid_h = nrows * cell_h;
            int area_h = 48;
            int start_y = 16 + (area_h - grid_h) / 2;
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
        /* Result: show final values */
        if(state->quantity == 1) {
            /* Single die: big centered number */
            canvas_set_font(canvas, FontBigNumbers);
            char val_str[8];
            snprintf(val_str, sizeof(val_str), "%d", state->results[0]);
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, val_str);
        } else {
            /* Multiple dice: each in a rounded frame, grid layout (3 cols max) */
            canvas_set_font(canvas, FontSecondary);

            int cols = (state->quantity <= 2) ? 2 : 3;
            int nrows = (state->quantity + cols - 1) / cols;
            int cell_w = 36;
            int cell_h = 16;
            int grid_w = cols * cell_w;
            int grid_h = nrows * cell_h;
            int area_top = 16;
            int area_bottom = 52;
            int area_h = area_bottom - area_top;
            int start_y = area_top + (area_h - grid_h) / 2;
            int start_x = (128 - grid_w) / 2;

            for(int i = 0; i < state->quantity; i++) {
                int col = i % cols;
                int row = i / cols;
                int cx = start_x + col * cell_w;
                int cy = start_y + row * cell_h;
                canvas_draw_rframe(canvas, cx + 1, cy + 1, cell_w - 2, cell_h - 2, 3);
                char val_str[8];
                snprintf(val_str, sizeof(val_str), "%d", state->results[i]);
                canvas_draw_str_aligned(canvas, cx + cell_w / 2, cy + cell_h / 2,
                    AlignCenter, AlignCenter, val_str);
            }

            char sum_str[32];
            snprintf(sum_str, sizeof(sum_str), "Sum: %d", state->sum);
            canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignBottom, sum_str);
        }

        /* Footer hint for result screen */
        if(state->quantity == 1) {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Reroll");
        }
    }
}

/* Draw callback */
static void dice_draw_callback(Canvas* canvas, void* ctx) {
    DiceState* state = (DiceState*)ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);

    switch(state->screen) {
    case DiceScreenSplash:
        draw_splash(canvas);
        break;
    case DiceScreenMain:
    case DiceScreenRolling:
    case DiceScreenResult:
        draw_main(canvas, state);
        break;
    }

    furi_mutex_release(state->mutex);
}

/* Input callback */
static void dice_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* queue = (FuriMessageQueue*)ctx;
    DiceEvent event;
    event.type = EventTypeInput;
    event.input = *input_event;
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

/* Timer callback */
static void dice_timer_callback(void* ctx) {
    FuriMessageQueue* queue = (FuriMessageQueue*)ctx;
    DiceEvent event;
    event.type = EventTypeTick;
    furi_message_queue_put(queue, &event, 0);
}

/* Generate random roll for a single die */
static int roll_die(int sides) {
    return (int)(furi_hal_random_get() % sides) + 1;
}

/* Roll all dice and compute sum */
static void roll_all_dice(DiceState* state) {
    int sides = dice_sides[state->dice_type];
    state->sum = 0;
    for(int i = 0; i < state->quantity; i++) {
        state->results[i] = roll_die(sides);
        state->sum += state->results[i];
    }
}

/* Entry point */
int32_t dice_roller_main(void* p) {
    UNUSED(p);

    /* Allocate state */
    DiceState* state = malloc(sizeof(DiceState));
    state->screen = DiceScreenSplash;
    state->dice_type = DiceTypeD6;
    state->quantity = 1;
    state->sum = 0;
    state->anim_tick = 0;
    for(int i = 0; i < MAX_DICE; i++) {
        state->results[i] = 0;
    }
    for(int i = 0; i < MAX_DICE; i++) {
        state->die_phase[i] = 0;
    }
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    /* Create message queue */
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(DiceEvent));

    /* Create viewport */
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, dice_draw_callback, state);
    view_port_input_callback_set(view_port, dice_input_callback, event_queue);

    /* Open GUI and attach viewport */
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    /* Open notification service for haptic feedback */
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);

    /* Create periodic timer at 50ms (20 ticks/sec) */
    FuriTimer* timer = furi_timer_alloc(dice_timer_callback, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, furi_ms_to_ticks(50));

    /* Record splash start time */
    uint32_t splash_start = furi_get_tick();

    /* Event loop */
    bool running = true;
    DiceEvent event;

    while(running) {
        FuriStatus status = furi_message_queue_get(event_queue, &event, 100);

        if(status != FuriStatusOk) {
            continue;
        }

        furi_mutex_acquire(state->mutex, FuriWaitForever);

        if(event.type == EventTypeTick) {
            /* Handle tick events */
            switch(state->screen) {
            case DiceScreenSplash:
                /* Check if splash duration has elapsed */
                if((furi_get_tick() - splash_start) >=
                   furi_ms_to_ticks(SPLASH_DURATION_MS)) {
                    state->screen = DiceScreenMain;
                }
                break;

            case DiceScreenRolling:
                state->anim_tick++;
                roll_all_dice(state);
                if(state->anim_tick >= TOTAL_ANIM_TICKS) {
                    roll_all_dice(state);
                    state->screen = DiceScreenResult;
                    notification_message(notification, &sequence_single_vibro);
                }
                break;

            default:
                break;
            }
        } else if(event.type == EventTypeInput) {
            /* Handle input events */

            /* Long press Back always exits */
            if(event.input.key == InputKeyBack &&
               event.input.type == InputTypeLong) {
                running = false;
            } else if(event.input.type == InputTypeShort) {
                switch(state->screen) {
                case DiceScreenSplash:
                    /* Any key skips splash */
                    state->screen = DiceScreenMain;
                    break;

                case DiceScreenMain:
                case DiceScreenResult:
                    switch(event.input.key) {
                    case InputKeyLeft:
                        /* Cycle dice type left */
                        if(state->dice_type == DiceTypeD4) {
                            state->dice_type = DiceTypeD20;
                        } else {
                            state->dice_type--;
                        }
                        break;

                    case InputKeyRight:
                        /* Cycle dice type right */
                        state->dice_type =
                            (state->dice_type + 1) % DiceTypeCount;
                        break;

                    case InputKeyUp:
                        /* Increase quantity */
                        if(state->quantity < MAX_DICE) {
                            state->quantity++;
                        }
                        break;

                    case InputKeyDown:
                        /* Decrease quantity */
                        if(state->quantity > 1) {
                            state->quantity--;
                        }
                        break;

                    case InputKeyOk:
                        /* Start rolling with random phase offsets */
                        state->anim_tick = 0;
                        for(int i = 0; i < state->quantity; i++) {
                            state->die_phase[i] = (int)(furi_hal_random_get() % DICE_SPRITE_FRAMES);
                        }
                        state->screen = DiceScreenRolling;
                        break;

                    case InputKeyBack:
                        /* Short press Back exits from main/result */
                        running = false;
                        break;

                    default:
                        break;
                    }
                    break;

                case DiceScreenRolling:
                    /* Ignore input during rolling animation */
                    break;
                }
            }
        }

        furi_mutex_release(state->mutex);
        view_port_update(view_port);
    }

    /* Cleanup in reverse order */
    furi_timer_stop(timer);
    furi_timer_free(timer);

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    view_port_free(view_port);
    furi_message_queue_free(event_queue);

    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}
