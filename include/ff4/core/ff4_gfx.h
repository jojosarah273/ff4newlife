/**
 * @file ff4_gfx.h
 * @brief Phase 10: ripped graphics decoding (platform-neutral).
 *
 * Decodes the SNES 2bpp planar tile data + BGB tilemaps + BGR555
 * palettes from the ripped blobs into RGBA surfaces the platform
 * renderers blit. Used by the title screen, prologue and later the
 * field/overworld backgrounds.
 */
#ifndef FF4_GFX_H
#define FF4_GFX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  Surfaces                                                            */
/* ==================================================================== */

typedef struct FF4_GfxSurface {
    int w, h;                 /**< Pixel dimensions. */
    uint32_t *pixels;         /**< 0xAARRGGBB, row-major. */
} FF4_GfxSurface;

void ff4_gfx_surface_free(FF4_GfxSurface *s);

/** Convert one SNES BGR555 color (from a 2-byte little-endian pair)
 *  to 0xAARRGGBB. */
uint32_t ff4_gfx_color16(const uint8_t *pal16, int index);

/* ==================================================================== */
/*  Tilemap compositing                                                 */
/* ==================================================================== */

/**
 * Compose a screen from a ripped (gfx, palette, tilemap) triple.
 * The tilemap holds 16-bit little-endian entries (SNES BG layout):
 *   bits 0-9   tile index
 *   bits 10-12 palette group (per bpp: 4/8/16 colors)
 *   bit  13    priority
 *   bit  14    horizontal flip
 *   bit  15    vertical flip
 * `bpp` is the tile bit depth (2/3/4) from the rip definition
 * (splash=4bpp, prologue=3bpp, window=2bpp) and drives both the plane
 * count and the palette group size.
 *
 * Returns 0 on success; the caller frees with ff4_gfx_surface_free().
 */
int ff4_gfx_render_tilemap(const FF4_AssetDB *db,
                           const char *gfx_key, const char *pal_key,
                           const char *tilemap_key,
                           int off_x, int off_y, int view_w, int view_h,
                           int bpp, FF4_GfxSurface *out);

/** Title screen (splash, 4bpp): 256x224. */
int ff4_gfx_title_screen(const FF4_AssetDB *db, FF4_GfxSurface *out);

/** Prologue stills (which 0..1): 256x224. */
int ff4_gfx_prologue(const FF4_AssetDB *db, int which, FF4_GfxSurface *out);

/* ==================================================================== */
/*  Field / overworld rendering (Phase 11: opening intro scenes)        */
/* ==================================================================== */

/**
 * Decode one field-map layout blob (the game's ff4-map RLE format:
 * [0x80|value][count] = value repeated count+1 times, raw otherwise)
 * into a 32x256 tile-byte buffer. Returns 8192 on success.
 */
int ff4_gfx_decode_layout(const uint8_t *blob, size_t len, uint8_t *outbuf);

/**
 * Field map BG entry for one decoded layout byte (mapTilesets lookup:
 * 4 x 256-byte tables: BG1 lo/hi at +0/+0x100, BG2 lo/hi at
 * +0x200/+0x300, per the game's $7F4800/$7F4880/... load). Returns the
 * 16-bit SNES entry (bits 0-9 tile, 10-12 palette, 14 vflip, 15 hflip).
 */
uint16_t ff4_gfx_map_entry(const uint8_t *tileset, int byte, int layer);

/**
 * Compose a 256x224 field-map view (BG1 + optional BG2 layer) around
 * camera tile (cx, cy). The map wraps 32 wide x 256 tall.
 * Returns 0 on success; caller frees with ff4_gfx_surface_free().
 */
int ff4_gfx_render_submap(const FF4_AssetDB *db, int map,
                          int cx, int cy, int vw, int vh,
                          bool bg2, FF4_GfxSurface *out);

/**
 * Compose a 256x224 world-map view (world = map type 0..2; layouts
 * worldLayout1/2/3, 4bpp tiles via the world tileset lookup).
 */
int ff4_gfx_render_world(const FF4_AssetDB *db, int world,
                         int cx, int cy, int vw, int vh,
                         FF4_GfxSurface *out);

/**
 * Blit one frame of a field sprite (mapSpriteGraphics, 4bpp, the
 * 16-tile-wide sheet order) onto a surface at pixel (x, y). Frames
 * are 16x16px animations; taller sprites stack sheets vertically.
 * color0 of the source is transparent.
 */
int ff4_gfx_draw_field_sprite(FF4_GfxSurface *out, int x, int y,
                              const FF4_AssetDB *db, int sprite,
                              int palette_group, int frame);

/** Blit one 16x16 world sprite (worldSpriteGraphics, 4bpp). */
int ff4_gfx_draw_world_sprite(FF4_GfxSurface *out, int x, int y,
                              const FF4_AssetDB *db, int sprite,
                              int palette_group, int frame);

/* ==================================================================== */
/*  PNG loading (modern resource files from tools/export_gfx.js)        */
/* ==================================================================== */

/** Load a PNG resource into an RGBA surface. Returns 0 on success. */
int ff4_image_load(const char *path, FF4_GfxSurface *out);

/** Write an RGBA buffer as a PNG (own zlib-free deflate writer). */
int ff4_png_write(const char *path, int w, int h, const uint32_t *px);

/* ==================================================================== */
/*  PPU: the 256x224 SNES picture pipeline                              */
/* ==================================================================== */

#define FF4_PPU_W 256
#define FF4_PPU_H 224

typedef struct FF4_Ppu {
    uint32_t px[FF4_PPU_W * FF4_PPU_H]; /**< 0xAARRGGBB frame. */
} FF4_Ppu;

/**
 * Compose the field BG (BG1 + optional BG2) for `map` into the frame
 * around camera tiles (cam_x, cam_y). The camera samples the 32x28-tile
 * window: x wraps 32, y wraps the 256-tall map. Anchored palette groups
 * (tile >> 4 & 7) are used unless `anchor_mode` disables them.
 */
void ff4_ppu_field_bg(FF4_Ppu *ppu, const FF4_AssetDB *db, int map,
                      int cam_x, int cam_y, bool bg2, int anchor_mode,
                      const uint8_t *map_pal_flat, size_t map_pal_len);

/** Project the per-map working palette from the flat MapPal (exact
 *  LoadMapPal emulation: x = palette_id << 7, colours at flat[0x10+x+i]
 *  lo bytes + flat[0x90+x+i] hi bytes). Writes 256 bytes (128 colors)
 *  into out. */
void ff4_ppu_project_field_pal(const uint8_t *map_pal_flat, size_t len,
                               int palette_id, uint8_t *out);

/** Compose the world map view (world 0..2) around camera tiles. */
void ff4_ppu_world_bg(FF4_Ppu *ppu, const FF4_AssetDB *db, int world,
                      int cam_x, int cam_y);

/** Blit one frame of a field sprite at pixel (x, y), color0 = opaque?
 *  (transparent = 0 clears the pixel). group = npcPalettes id. */
void ff4_ppu_sprite(FF4_Ppu *ppu, const FF4_AssetDB *db, int sprite,
                    int group, int frame, int x, int y, bool transparent);

/** Apply SNES-style brightness color math: level 15 = unchanged, 0 = black. */
void ff4_ppu_brightness(FF4_Ppu *ppu, int level);

/** 8x8 glyph bitmap for an ASCII code (32..127); 8 bytes, MSB = left pixel.
 *  Returns NULL for out-of-range codes. Used by hosts that draw text
 *  themselves. */
const uint8_t *ff4_font_glyph(int ascii);

/** Fast clear + fill helpers for overlays. */
void ff4_ppu_fill(FF4_Ppu *ppu, uint32_t color);
void ff4_ppu_text(FF4_Ppu *ppu, int x, int y, const char *s,
                  uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* FF4_GFX_H */