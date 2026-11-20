/**
 * @file sdl_input.c
 * @brief SDL2 adapter for the input API: maps keyboard/gamepad to the
 *        unified FF4 button model (field + menu + battle use one state).
 */
#include <SDL.h>

#include "ff4/platform/input_api.h"

static const SDL_Scancode map[FF4_BUTTON_COUNT] = {
    [FF4_BUTTON_UP] = SDL_SCANCODE_UP,
    [FF4_BUTTON_DOWN] = SDL_SCANCODE_DOWN,
    [FF4_BUTTON_LEFT] = SDL_SCANCODE_LEFT,
    [FF4_BUTTON_RIGHT] = SDL_SCANCODE_RIGHT,
    [FF4_BUTTON_A] = SDL_SCANCODE_X,
    [FF4_BUTTON_B] = SDL_SCANCODE_Z,
    [FF4_BUTTON_X] = SDL_SCANCODE_S,
    [FF4_BUTTON_Y] = SDL_SCANCODE_A,
    [FF4_BUTTON_L] = SDL_SCANCODE_Q,
    [FF4_BUTTON_R] = SDL_SCANCODE_W,
    [FF4_BUTTON_START] = SDL_SCANCODE_RETURN,
    [FF4_BUTTON_SELECT] = SDL_SCANCODE_BACKSPACE
};

int ff4_input_init(void)
{
    return 0;
}

void ff4_input_poll(FF4_InputState *out)
{
    int i;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    for (i = 0; i < FF4_BUTTON_COUNT; ++i) {
        out->held[i] = keys[map[i]] != 0;
        out->pressed[i] = out->held[i] && !out->previous[i];
        out->previous[i] = out->held[i];
    }
    out->quit = false;
}

void ff4_input_quit(void)
{
}