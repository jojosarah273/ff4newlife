/**
 * @file input_api.h
 * @brief Unified button input model. One state for field, menu and battle;
 *        backends (SDL2, ...) fill it each frame. Matches the game's
 *        controller state bytes ($0100/$0600: held, pressed, repeat).
 */
#ifndef FF4_INPUT_API_H
#define FF4_INPUT_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FF4_Button {
    FF4_BUTTON_UP = 0,     /**< D-pad up (also menu cursor up). */
    FF4_BUTTON_DOWN = 1,
    FF4_BUTTON_LEFT = 2,
    FF4_BUTTON_RIGHT = 3,
    FF4_BUTTON_A = 4,      /**< Confirm. */
    FF4_BUTTON_B = 5,      /**< Cancel. */
    FF4_BUTTON_X = 6,      /**< Menu. */
    FF4_BUTTON_Y = 7,      /**< Secondary/cancel. */
    FF4_BUTTON_L = 8,
    FF4_BUTTON_R = 9,
    FF4_BUTTON_START = 10, /**< Pause/menu. */
    FF4_BUTTON_SELECT = 11,
    FF4_BUTTON_COUNT = 12
} FF4_Button;

/**
 * Holds per-button edge info. `pressed[i]` is true on the frame a button
 * went down; `held[i]` while it stays down (mirrors $0100-$0108 handling).
 */
typedef struct FF4_InputState {
    bool held[FF4_BUTTON_COUNT];
    bool pressed[FF4_BUTTON_COUNT];
    bool previous[FF4_BUTTON_COUNT]; /**< Backend write/tracking area. */
    bool quit;                       /**< Backend requests exit. */
} FF4_InputState;

/* Backend interface (implemented per platform under src/platform/). */
int  ff4_input_init(void);
void ff4_input_poll(FF4_InputState *out);
void ff4_input_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* FF4_INPUT_API_H */