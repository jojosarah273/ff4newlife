/**
 * @file world_view.c
 * @brief Engine piece: the world-map viewport.
 *
 * In the game only a 256x224 window of the world is visible, and the
 * actor moves inside it while the camera follows.  That is exactly what
 * this does - no mode 7, no HDMA: the world is a bitmap, the viewport is
 * a rectangle, the camera keeps the actor centred (clamped to the world).
 *
 *   world map     2048x2048 px (overworld / underground), 512x512 (moon)
 *   viewport      256x224 logical px, integer-scaled to the window
 *   actor         world pixel position, moved with the arrow keys
 *   camera        actor - viewport/2, clamped to the world bounds
 *
 * Controls:  arrows / WASD  move the actor (camera follows)
 *            T              toggle whole-map view / viewport view
 *            +/- or wheel   zoom (viewport view)
 *            1 / 2 / 3      overworld / underground / moon
 *            R              recentre
 *            Esc / Q        quit
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#ifdef FF4_HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include "ff4/core/ff4_gfx.h"
#include "ff4/core/ff4_window_text.h"
#include "ff4/core/ff4_dialog_layout.h"
#include "ff4/platform/app_paths.h"

#define WIN_DEFAULT_W 1024
#define WIN_DEFAULT_H 768
/* runtime window size (the window is resizable, and fullscreen changes it) */
static int win_w = WIN_DEFAULT_W;
static int win_h = WIN_DEFAULT_H;

/* FF4_NOCLIP / the N key: disable collision so the renderer (including the
 * overhang layer) can be inspected without being blocked by geometry. */
static int noclip;
/* C key: tint every visible cell green (walkable) / red (blocked) so the
 * collision table can be checked against the real map by eye. */
static int show_coll;
/* Player z-level, mirroring $d2 in move.asm: rebuilt from the tile the player
 * stands on (byte1 & 0x03) and HELD while standing on a bridge (byte1 & 0x04).
 * Water/aquifer and bridge tiles are not flat passable/impassable -- moving is
 * gated on this z matching the destination's z bits. */
static int player_z = 2;
/* global frame counter: NPC idle animation uses this (the game uses $7a for
 * NPCs and $7b for the player, which is why they are not in sync) */
static unsigned frame_counter;
/* NPC palette search: H/J shift every NPC palette so the right one can be
 * found by eye; FF4_NPCPAL sets the starting shift. */
/* Found in game by eye: the correct shift differs per map. Baron town is 13,
 * Village of Mist is 5. FF4_NPCPAL, and the [ ] keys, override the table. */
static int npc_pal_shift = 0;
static int npc_pal_forced = 0;

/* Story sprites that appear only under particular event switches. They are drawn
 * from the map's NPC file like any other, so they are filtered by cell here until
 * the event conditions can be evaluated. Positions reported in game; see
 * ff4_cell.txt. */
static const struct { int map, x, y; const char *what; } hidden_npc[] = {
    { 1,  8, 16, "Village of Mist: Kain"  },
    { 1, 14,  9, "Village of Mist: Rydia" },
};

static int npc_is_hidden(int map, int x, int y)
{
    size_t k;
    for (k = 0; k < sizeof hidden_npc / sizeof hidden_npc[0]; ++k)
        if (hidden_npc[k].map == map && hidden_npc[k].x == x && hidden_npc[k].y == y)
            return 1;
    return 0;
}

/* The map's shift is right for most sprites but not all: the game picks each
 * NPC's palette from the map's two (npcPalette1/2), and a few records in the rip
 * carry the other one. Individual cells can be corrected here.
 * Report the cell and the palette that matches, e.g. { 2, 16, 10, 12, "Kaipo" }. */
static const struct { int map, x, y, pal; const char *what; } npc_pal_fix[] = {
    /* { 2, 16, 10, 12, "Kaipo: dancer" }, */
};

/* cast to int: with an empty table the comparison would be `unsigned < 0`,
 * which gcc flags as always-false */
#define NPC_PAL_FIX_COUNT \
    ((int)(sizeof npc_pal_fix / sizeof npc_pal_fix[0]))

static int npc_pal_fix_for(int map, int x, int y, int *out)
{
    int k;
    for (k = 0; k < NPC_PAL_FIX_COUNT; ++k)
        if (npc_pal_fix[k].map == map && npc_pal_fix[k].x == x && npc_pal_fix[k].y == y) {
            *out = npc_pal_fix[k].pal;
            return 1;
        }
    return 0;
}

/* The exported .npc data now carries ABSOLUTE palette indices, taken from
 * data/npc_pal.json (measured in game: Baron 13/1, Mist 5/13).  So the engine
 * adds no base -- H/J remain as a debug offset for searching new maps. */
static int npc_pal_for_map(int map)
{
    (void)map;
    return 0;
}
static int sm_npc_dirty;
static int curtain_this_frame;   /* warp curtain state of the frame just drawn */
/* Mosaic transition (field/event.asm EventCmd_d2 + EventMosaicTbl @fa66):
 * $2106 ramps the block size 0 -> 15 -> 0 over 64 frames (2 frames per step).
 * We apply the same thing as a post pass on the viewport. */
static int mosaic_blk;           /* 1 = off, N = NxN blocks */

static const uint8_t event_mosaic_tbl[32] = {
    0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F,
    0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF, 0xFF,
    0xEF, 0xDF, 0xCF, 0xBF, 0xAF, 0x9F, 0x8F, 0x7F,
    0x6F, 0x5F, 0x4F, 0x3F, 0x2F, 0x1F, 0x0F, 0x0F
};
static int show_npcpal = 0;    /* palette readout: K toggles (debug only) */
/* NPCs hold still, for comparing palettes against the real game. Toggled with F,
 * or FF4_NPCFREEZE=1 to start frozen. Animation stops too. */
static int npc_frozen = 0;

/* ---- Palette Inspector Mode ---- */
static int pal_inspect_mode = 0;
static int pal_inspect_npc = -1;  /* selected NPC index, -1 = none */
static int pal_inspect_recorded[32];  /* recorded palettes for each NPC */

/* ---- sub-map state (data/submaps/<id>.{png,over.png,walk,props}) ---- */
static int sm_map = -1;
/* ---- ff4map runtime packs (tools/ff4data.py; docs/ARCHITECTURE.md) ----
 * The uniform data layer: per-map cell arrays + shared art.  The engine
 * renders per cell from the atlas instead of loading baked PNGs; the
 * exporter's PNGs remain the VERIFICATION references for the guards. */
static uint8_t pk_cells[2][1024];      /* BG1 cells, group 0/1 */
static uint8_t pk_bg2c[2][1024];        /* BG2 cells, group 0/1 (0xFF = none) */
static int pk_loaded;                   /* pack available for this map */
static int pk_has_g1, pk_ts, pk_sheet_id, pk_pal_line;
static uint8_t *pk_flags;               /* 0x400 (lo,hi) per sub-tile */
static uint8_t *pk_sheet_idx;           /* 0x400 tiles x 64 idx bytes */
static uint8_t *pk_anim_idx;            /* 4 phases x 16 slots x 64 */
static uint8_t *pk_palettes;            /* lines x 8 x 8 x RGBA */
static SDL_Texture *pk_atlas_o;         /* opaque (BG2): alpha 255 everywhere */
static SDL_Texture *pk_atlas_t;         /* transparent (BG1): alpha 0 at idx 0 */
#define PK_ATLAS_COLS 64
#define PK_ATLAS_ROWS 17                /* 16 static + 1 animated (4 phases) */

/* ---- world packs (data/world/<name>.ff4world; tools/ff4data.py) ---- */
static uint8_t *wp_cells;            /* w*h composed-cell ids */
static uint8_t *wp_base;             /* 128*4 */
static uint8_t *wp_assign;           /* 256 */
static uint8_t *wp_gfx;              /* nart*64 art bytes (8bpp expanded) */
static uint8_t *wp_pal;              /* 64*4 RGBA */
static int wp_w, wp_h, wp_nart, wp_loaded;

static void wp_free(void)
{
    free(wp_cells); wp_cells = NULL;
    free(wp_base); wp_base = NULL;
    free(wp_assign); wp_assign = NULL;
    free(wp_gfx); wp_gfx = NULL;
    free(wp_pal); wp_pal = NULL;
    wp_loaded = 0;
}

static int wp_load(const char *name)
{
    char path[512];
    uint8_t hdr[12];
    FILE *f;
    size_t n;

    wp_free();
    snprintf(path, sizeof path, "data/world/%s.ff4world", name);
    if (!ff4_resolve_path(path, path, sizeof path))
        return -1;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "FFW1", 4) != 0) {
        fclose(f);
        return -1;
    }
    wp_w = hdr[4] | (hdr[5] << 8);
    wp_h = hdr[6] | (hdr[7] << 8);
    wp_nart = hdr[8] | (hdr[9] << 8);
    n = (size_t)wp_w * wp_h;
    wp_cells = (uint8_t *)malloc(n);
    wp_base = (uint8_t *)malloc(512);
    wp_assign = (uint8_t *)malloc(256);
    wp_gfx = (uint8_t *)malloc((size_t)wp_nart * 64);
    wp_pal = (uint8_t *)malloc(64 * 4);
    if (!wp_cells || !wp_base || !wp_assign || !wp_gfx || !wp_pal ||
        fread(wp_cells, 1, n, f) != n ||
        fread(wp_base, 1, 512, f) != 512 ||
        fread(wp_assign, 1, 256, f) != 256 ||
        fread(wp_gfx, 1, (size_t)wp_nart * 64, f) != (size_t)wp_nart * 64 ||
        fread(wp_pal, 1, 64 * 4, f) != 64 * 4) {
        fclose(f);
        wp_free();
        return -1;
    }
    fclose(f);
    wp_loaded = 1;
    return 0;
}

static void pk_free_art(void)
{
    free(pk_flags); pk_flags = NULL;
    free(pk_sheet_idx); pk_sheet_idx = NULL;
    free(pk_anim_idx); pk_anim_idx = NULL;
    free(pk_palettes); pk_palettes = NULL;
    if (pk_atlas_o) { SDL_DestroyTexture(pk_atlas_o); pk_atlas_o = NULL; }
    if (pk_atlas_t) { SDL_DestroyTexture(pk_atlas_t); pk_atlas_t = NULL; }
    pk_loaded = 0;
}

static int pk_load(int id, int group)
{
    char path[512];
    FILE *f;
    uint8_t hdr[12];
    long n;

    pk_loaded = 0;
    pk_has_g1 = 0;
    snprintf(path, sizeof path, "data/maps/%d.ff4map", id);
    if (!ff4_resolve_path(path, path, sizeof path))
        return -1;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "FFM1", 4) != 0) {
        fclose(f);
        return -1;
    }
    pk_ts = hdr[4] | (hdr[5] << 8);
    pk_sheet_id = hdr[6] | (hdr[7] << 8);
    pk_pal_line = hdr[8];
    pk_has_g1 = hdr[10];
    fseek(f, 12, SEEK_SET);
    if (fread(pk_cells[0], 1, 1024, f) != 1024 ||
        fread(pk_bg2c[0], 1, 1024, f) != 1024) {
        fclose(f);
        return -1;
    }
    if (group && pk_has_g1) {
        fseek(f, 12 + 4096, SEEK_SET);
        if (fread(pk_cells[1], 1, 1024, f) != 1024 ||
            fread(pk_bg2c[1], 1, 1024, f) != 1024) {
            fclose(f);
            return -1;
        }
    } else if (group) {
        memcpy(pk_cells[1], pk_cells[0], 1024);
        memcpy(pk_bg2c[1], pk_bg2c[0], 1024);
    }
    fclose(f);
    pk_loaded = 1;
    return 0;
}            /* -1 = on a world map */
/* FF4_WARPTO may name any map; most maps are not a warp destination, so when
 * no warp entry matches we synthesise a direct jump instead of silently
 * showing the first warp's target. */
static int force_map = -1;
static FF4_GfxSurface sm_img;
static FF4_GfxSurface sm_over;     /* upper z-level cells, RGBA */
static uint8_t sm_walk[1024];
static uint8_t sm_props[2048];     /* 2 bytes per cell: tile b1, b2 */
static uint8_t sm_tileidx[1024];   /* tile index per cell (255 = empty) */
static uint8_t sm_dirs[1024];      /* allowed directions per cell (0x0F = all) */

/* --- NPCs (data/submaps/<id>.npc): "x y dir pal sheet<TAB>text" --- */
#define SM_NPC_MAX 32
static struct SmNpc {
    int x, y, dir, pal, sheet;
    int pal_raw;                   /* raw palette field (0-3) from .npc file */
    int pal_fix;                   /* absolute palette, -1 = use computed or pal + shift */
    int empty;
    int random, speed;             /* randomMovement, speed 0..3 */
    int animated;                  /* idle animation on/off */
    int facing, step_px, step_dir, wait;
    int acc;                       /* $08ff: direction-repath accumulator */
    int push;                      /* frames left of 'step aside' for the player */
    int z;                         /* this NPC's z-level (mirrors the player) */
    SDL_Texture *tex[8];           /* [facing*2 + frame] */
    int built;
    char text[320];
} sm_npc[SM_NPC_MAX];
static int sm_npc_count;

static void pal_inspect_init(void)
{    int i;
    pal_inspect_mode = 1;
    pal_inspect_npc = sm_npc_count > 0 ? 0 : -1;
    for (i = 0; i < 32; i++)
        pal_inspect_recorded[i] = -1;  /* -1 = not recorded */
    printf("[palette_inspect] mode enabled, %d NPCs\n", sm_npc_count);
    if (pal_inspect_npc >= 0) {
        printf("[palette_inspect] selected NPC %d at (%d,%d) sheet=%d pal=%d\n",
               pal_inspect_npc, sm_npc[pal_inspect_npc].x,
               sm_npc[pal_inspect_npc].y, sm_npc[pal_inspect_npc].sheet,
               sm_npc[pal_inspect_npc].pal);
    }
}

#define SM_ANIM_PHASES 4
static int sm_w, sm_h;             /* cells */
static int sm_tiles = 32;          /* cells per side (32) */
static int sm_tex_dirty;
static SDL_Texture *sm_tex;
static SDL_Texture *sm_over_tex;
/* Tile animation: field/bg_anim.asm TfrBGAnimGfx advances the phase every
 * 8 frames through 4 phases ($7a & 0x18, gated by $7a & 0x06 == 0).
 * data/submaps/<id>.anim0..3.png hold only the animated cells. */
static FF4_GfxSurface sm_anim[SM_ANIM_PHASES];
static FF4_GfxSurface sm_over_ph[SM_ANIM_PHASES];
static SDL_Texture *sm_over_tex_ph[SM_ANIM_PHASES];
static SDL_Texture *sm_anim_tex[SM_ANIM_PHASES];
static int sm_anim_count;

/* Cell triggers (field/trigger.asm; MapTriggers1/2 decoded by the rip).
 * data/submaps/<id>.trig: "x y map xDest yDest dir event" - fired when the
 * party ENTERS that cell. */
#define SM_TRIG_MAX 32
static struct { int x, y, map, dx, dy, dir, event; } sm_trig[SM_TRIG_MAX];
/* $1701: 0 when the map was entered from the overworld, 1 from the
 * underground/moon.  Selects <id>.trig or <id>.trig1. */
static int sm_trig_group;

/* Same $1701 selects the LAYOUT group (DecodeSubTilemap @feba): group 1
 * reads its tilemap from SubTilemap2 (pointer word 256+id, base 18/8000)
 * - the underground/moon render of the SAME map id.  Emulator-proven for
 * group 0 (the intro airship scene == map 381, group 0, 386/390 cells);
 * group 1 is the ASM rule.  The rip's mapLayouts2 == group 1. */
static int sm_layout_group;

/* filename infix for the layout group ("" or ".g1"), with fallback:
 * only maps whose group-1 layout differs (and layout ids < 128) have a
 * .g1 export.  FF4_LAYOUTG1=1 forces it for testing. */
static const char *sm_layout_infix(int id)
{
    static int cache_id = -1, cache_group = -1, cache_yes;
    int want = sm_layout_group;
    FILE *f;
    char p[512];

    {   /* FF4_LAYOUTG1 overrides in BOTH directions (0 = force group 0) */
        const char *e = getenv("FF4_LAYOUTG1");

        if (e)
            want = atoi(e) ? 1 : 0;
    }
    if (!want)
        return "";
    if (id == cache_id && want == cache_group)
        return cache_yes ? ".g1" : "";
    cache_id = id;
    cache_group = want;
    snprintf(p, sizeof p, "data/submaps/%d.g1.png", id);
    cache_yes = ff4_resolve_path(p, p, sizeof p);
    return cache_yes ? ".g1" : "";
}

static int sm_trig_count;

/* Previous-map stack for exit tiles (props2 & 0x10).  FF4 keeps a ring of
 * previous maps ($172C); we only need to come back from a door. */
#define MAP_STACK_MAX 8
static struct { int is_world, id, cx, cy; } map_stack[MAP_STACK_MAX];
static int map_depth;

/* a pending trigger warp, applied at the curtain midpoint */
static int tw_active;
static struct { int map, x, y, dir, event; } tw;
static int tw_reported;
static int warp_frames;   /* defined below; needed by trigger_warp() */

static void load_sprite_slots(void);
static void sm_trig_load(int id);
static int  sm_trig_find(int cx, int cy);
static void trigger_warp(int map, int x, int y, int dir, int event);
static void actor_sprites_for_map(int map, int char_id);
static void sprite_load(int sheet, int pal);
static int sprite_sheet_id;
static int sprite_pal_id;

/* Content box of the loaded sub-map, in pixels.  A shop interior is a 9x11
 * room inside the 32x32-cell (512x512) canvas; the rest of the canvas is
 * unused.  The camera centres this box when it is smaller than the viewport
 * and clamps to it otherwise, so small rooms sit in the middle of the screen
 * with black around them instead of hugging the top-left corner. */
static int sm_box_x0, sm_box_y0, sm_box_x1, sm_box_y1;

static void sm_compute_box(void)
{
    int x, y, x0 = -1, y0 = -1, x1 = -1, y1 = -1;

    sm_box_x0 = 0; sm_box_y0 = 0;
    sm_box_x1 = sm_img.w; sm_box_y1 = sm_img.h;
    if (!sm_img.pixels)
        return;
    for (y = 0; y < sm_img.h; ++y)
        for (x = 0; x < sm_img.w; ++x) {
            uint32_t p = sm_img.pixels[(size_t)y * sm_img.w + x];

            if ((p & 0x00ffffffu) == 0)      /* black = unused cell */
                continue;
            if (x0 < 0 || x < x0) x0 = x;
            if (y0 < 0 || y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    if (x0 >= 0) {
        sm_box_x0 = x0 & ~15;              /* snap to cell edges */
        sm_box_y0 = y0 & ~15;
        sm_box_x1 = (x1 | 15) + 1;
        sm_box_y1 = (y1 | 15) + 1;
    }
}

/* forward declarations of the view globals defined below */
static SDL_Renderer *ren;
static double cam_x, cam_y, zoom;
static int vw, vh;
static uint8_t *read_file(const char *path, size_t *len);
/* draw the world viewport per cell from the pack (docs/ARCHITECTURE.md
 * step 1) into an ARGB buffer - one pass, direct pixel writes.
 * Equivalent to compose_view()'s .idx/.pal path - byte-verified by
 * tools/ff4data.py --verify against the reference world PNGs. */
static void wp_pass(uint32_t *out, int ou, int ov)
{
    int cx0 = (int)(cam_x / 16), cy0 = (int)(cam_y / 16);
    int cxx, cyy;

    if (!wp_loaded)
        return;
    for (cyy = cy0; cyy <= cy0 + ov / 16; ++cxx == 0 ? 0 : 0)
        ;
    for (cyy = cy0; cyy <= cy0 + ov / 16 + 1; ++cyy)
        for (cxx = cx0; cxx <= cx0 + ou / 16 + 1; ++cxx) {
            int cx = cxx & (wp_w - 1), cy = cyy & (wp_h - 1);
            int c = wp_cells[(size_t)cy * wp_w + cx];
            int k;

            for (k = 0; k < 4; ++k) {
                int bt = wp_base[c * 4 + k];
                int pb = wp_assign[bt];
                const uint8_t *art = wp_gfx + (size_t)bt * 64;
                int ox = (cxx * 16 + (k % 2) * 8) - (int)cam_x;
                int oy = (cyy * 16 + (k / 2) * 8) - (int)cam_y;
                int y, x;

                for (y = 0; y < 8; ++y) {
                    int sy = oy + y;

                    if (sy < 0 || sy >= ov)
                        continue;
                    for (x = 0; x < 8; ++x) {
                        int sx = ox + x;
                        int ci;
                        const uint8_t *cc;

                        if (sx < 0 || sx >= ou)
                            continue;
                        ci = (art[y * 8 + x] + pb) % 64;
                        cc = wp_pal + ci * 4;
                        out[(size_t)sy * ou + sx] =
                            0xFF000000u | ((uint32_t)cc[0] << 16) |
                            ((uint32_t)cc[1] << 8) | cc[2];
                    }
                }
            }
        }
}

