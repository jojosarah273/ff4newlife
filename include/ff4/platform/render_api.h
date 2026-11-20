/**
 * @file render_api.h
 * @brief Abstract draw-call API. Core subsystems (field, battle, menu)
 *        submit high-level commands; backends translate them to SDL2,
 *        Raylib, native 3DS/Dreamcast etc. No SNES PPU registers here.
 */
#ifndef FF4_RENDER_API_H
#define FF4_RENDER_API_H

#include "ff4/core/ff4_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Backend lifecycle (implemented per platform under src/platform/). */
int  ff4_render_init(int width, int height, int fullscreen);
void ff4_render_begin(void);   /**< Start a frame (clear etc.). */
void ff4_render_end(void);     /**< Present the frame. */
void ff4_render_quit(void);

/* High-level commands used by later phases:
   - ff4_render_tilemap(map_id, x, y, camera)       (Phase 6)
   - ff4_render_sprite(...)                          (Phase 6/7)
   - ff4_render_text(...)                            (Phase 7) */

/** Blit a decoded surface (scaled) - used by the title/opening scenes. */
void ff4_render_blit(const FF4_GfxSurface *s, int x, int y, int scale);

/** Debug/demo primitives for the debug renderer (implemented by SDL). */
void ff4_render_rect(int x, int y, int w, int h,
                     int r, int g, int b);
void ff4_render_text(int x, int y, const char *text,
                     int r, int g, int b);

#ifdef __cplusplus
}
#endif

#endif /* FF4_RENDER_API_H */