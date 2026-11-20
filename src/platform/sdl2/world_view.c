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


/* ================================================================== */
/*  ENGINE BODY - recovery stage 1 (the world view)                   */
/*                                                                    */
/*  Everything below was rebuilt after the truncation incident        */
/*  (commit 2d695b5).  Stage 1 scope: boot, camera, movement, the      */
/*  world view from the .ff4world packs (wp_pass) with the .idx/.pal  */
/*  compose_view as the A/B fallback (FF4_NOWORLDPACK=1), the actor    */
/*  sprite from the ROM's PlayerSpriteTiles, the lava palette cycle   */
/*  (UpdateLavaPal @c4cd), and the [wtile] cell reporter.             */
/*                                                                    */
/*  Guards that lock this stage: verify_world_report.py,              */
/*  verify_lava_pal.py, and the pack-vs-idx A/B screenshot test.      */
/*  Sub-maps (FF4_WARPTO) return in stage 2.                           */
/* ================================================================== */

static uint8_t *read_file(const char *path, size_t *len)
{
    char full[512];
    FILE *f;
    long n;
    uint8_t *buf;

    if (!ff4_resolve_path(path, full, sizeof full))
        return NULL;
    f = fopen(full, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n ? n : 1);
    if (n && fread(buf, 1, n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    if (len)
        *len = n;
    return buf;
}

/* ----------------------- world map data -------------------------- */

typedef struct World {
    const char *name;          /* display name for [wtile] reports */
    const char *stem;          /* file stem (lowercase) */
    const char *file;          /* data/world/<stem>.png */
    FF4_GfxSurface img;        /* the composed world PNG (whole-map view) */
    int loaded;
    uint8_t *idx;              /* (cells*16)^2 palette indices, 1 byte/px */
    uint8_t *pal;              /* 64 RGBA colors as stored */
    uint8_t *work_pal;         /* pal copy with the lava cycle applied */
    uint8_t *lavapal;          /* 4 sets x 8 BGR555 (underground only) */
    int idx_w, idx_h;          /* pixels */
    int cells;                 /* map cells per side */
    uint8_t *walk;             /* cells*cells byte1 per cell */
    uint8_t *tiles;            /* cells*cells composed tile per cell */
    uint8_t *base;             /* composed tile -> 4 base tiles (128*4) */
    uint8_t *assign;           /* base tile -> palette base (256) */
    uint8_t *props;            /* cells*cells byte2 per cell */
} World;

/* the world map is 256x256 cells (4096x4096 px); the moon is 64x64
 * (1024x1024 px).  Cell = one 16x16 composed tile. */
static World worlds[3] = {
    { .name = "Overworld",   .stem = "overworld",
      .file = "data/world/overworld.png" },
    { .name = "Underground", .stem = "underground",
      .file = "data/world/underground.png" },
    { .name = "Moon",        .stem = "moon",
      .file = "data/world/moon.png" },
};
static int cur = 0;                 /* current world index */
static World *W = &worlds[0];

/* world pixel position of the party (the sprite is centred on it) */
static double ax = 15 * 16 + 8, ay = 16 * 16 + 8;

/* LavaPalTbl (field/pal_anim.asm @c4e0): one step every 16 frames,
 * full cycle 128 frames.  Steps 0..7 select LavaAnimPal sets. */
static const uint8_t lava_pal_tbl[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
/* FF4_LAVASTEP pins the step index (0..7); -1 = natural cadence */
static int lava_step_env = -1;

/* UpdateLavaPal @c4cd: the underground cycles the 8 colors at
 * palette entries 18..25 (CGRAM group 1) through 4 sets of
 * LavaAnimPal.  Set k lives at lavapal[k*16 .. k*16+15] as BGR555. */
static void world_patch_palette(World *w, int step)
{
    int k;

    if (!w->lavapal)
        return;
    if (step < 0 || step > 7)
        step = 0;
    for (k = 0; k < 8; ++k) {
        unsigned v = w->lavapal[lava_pal_tbl[step] * 16 + k * 2] |
                     ((unsigned)w->lavapal[lava_pal_tbl[step] * 16 + k * 2 + 1] << 8);
        unsigned r = ((v & 0x1F) << 3) | ((v >> 2) & 7);
        unsigned g = (((v >> 5) & 0x1F) << 3) | ((v >> 7) & 7);
        unsigned b = (((v >> 10) & 0x1F) << 3) | ((v >> 12) & 7);

        w->work_pal[(18 + k) * 4 + 0] = (uint8_t)r;
        w->work_pal[(18 + k) * 4 + 1] = (uint8_t)g;
        w->work_pal[(18 + k) * 4 + 2] = (uint8_t)b;
        w->work_pal[(18 + k) * 4 + 3] = 0xFF;
    }
}

/* keep the pack's palette in sync with the lava cycle (wp_pass reads
 * wp_pal directly; entries 18..25 are the cycled ones) */
static void lava_sync_pack(World *w)
{
    if (wp_loaded && wp_pal && w->work_pal)
        memcpy(wp_pal + 18 * 4, w->work_pal + 18 * 4, 8 * 4);
}

static int load_world(int n)
{
    World *w = &worlds[n];
    char p[512];

    if (w->loaded)
        return 0;
    if (n < 0 || n > 2)
        return -1;
    if (ff4_image_load(w->file, &w->img) != 0) {
        fprintf(stderr, "[world] cannot load %s\n", w->file);
        return -1;
    }
    snprintf(p, sizeof p, "data/world/%s.idx", w->stem);
    w->idx = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.pal", w->stem);
    w->pal = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.walk", w->stem);
    w->walk = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.tiles", w->stem);
    w->tiles = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.base", w->stem);
    w->base = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.assign", w->stem);
    w->assign = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.props", w->stem);
    w->props = read_file(p, NULL);
    snprintf(p, sizeof p, "data/world/%s.lavapal", w->stem);
    w->lavapal = read_file(p, NULL);

    w->cells = (n == 2) ? 64 : 256;
    w->idx_w = w->idx_h = w->cells * 16;

    if (!w->idx || !w->pal) {
        fprintf(stderr, "[world] %s missing .idx/.pal data\n", w->name);
        return -1;
    }
    w->work_pal = (uint8_t *)malloc(64 * 4);
    memcpy(w->work_pal, w->pal, 64 * 4);
    world_patch_palette(w, 0);

    /* the per-world pack (data/world/<name>.ff4world); FF4_NOWORLDPACK
     * forces the compose_view (.idx/.pal) path for the A/B check */
    wp_free();
    wp_loaded = 0;
    if (getenv("FF4_NOWORLDPACK"))
        printf("[world] pack disabled (FF4_NOWORLDPACK), compose_view path\n");
    else if (wp_load(w->stem) != 0)
        printf("[world] no pack for %s, compose_view path\n", w->name);
    lava_sync_pack(w);

    cur = n;
    W = w;
    w->loaded = 1;
    return 0;
}

/* on-foot world passability (CheckPlayerMoveWorld, move.asm): the
 * walk table holds byte1 per cell; bit 1 is walkable on foot.  The
 * recorded V/B cell reports agree (byte1 0x02/0x03 pass, 0x00/0x01
 * blocked).  FF4_NOCLIP bypasses. */
static int world_passable(int cx, int cy)
{
    if (noclip || !W->walk)
        return 1;
    if (cx < 0 || cy < 0 || cx >= W->cells || cy >= W->cells)
        return 0;
    return (W->walk[(size_t)cy * W->cells + cx] & 0x02) ? 1 : 0;
}

/* -------------------------- camera -------------------------------- */

/* camera keeps the actor centred, clamped to the world bounds */
static void update_camera(void)
{
    cam_x = ax - vw / 2.0;
    cam_y = ay - vh / 2.0;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    if (cam_x > W->idx_w - vw) cam_x = W->idx_w - vw;
    if (cam_y > W->idx_h - vh) cam_y = W->idx_h - vh;
}

/* ------------------------- actor sprite --------------------------- */

/* PlayerSpriteTiles @c0c4 (ROM, byte-verified): frame = facing*2+step,
 * each frame = 4 bytes (tile, attrs).  Attrs bit 6 (0x40) = hflip.
 * Facing from move.asm $1705: 0=up 1=right 2=down 3=left. */
static const uint8_t player_sprite_tiles[8][8] = {
    /* facing 0 = up */
    { 4,0x20, 5,0x20, 6,0x20, 7,0x20 },
    { 4,0x20, 5,0x20, 7,0x60, 6,0x60 },
    /* facing 1 = right (side art hflipped) */
    { 9,0x60, 8,0x60,11,0x60,10,0x60 },
    {13,0x60,12,0x60,15,0x60,14,0x60 },
    /* facing 2 = down */
    { 0,0x20, 1,0x20, 2,0x20, 3,0x20 },
    { 0,0x20, 1,0x20, 3,0x60, 2,0x60 },
    /* facing 3 = left */
    { 8,0x20, 9,0x20,10,0x20,11,0x20 },
    {12,0x20,13,0x20,14,0x20,15,0x20 },
};

/* party leader = mapSpriteGraphics slot 0 (Cecil), palette from
 * map_sprite_slots.bin slot 0 (pal 1).  The sheet is 4bpp SNES
 * planar, expanded at load to 64 index bytes per tile. */
static uint8_t *spr_sheet;          /* tiles*64 palette indices, MSB-left */
static int spr_sheet_len;
static uint8_t spr_rgba[8][4];       /* 8 colors RGBA from npcPalettes[n] */
static int spr_loaded;

/* facing & animation state ($1705, $7b, $06 in DrawPlayerSub) */
static int facing = 2;              /* down */
static int moving;                  /* $ab */
static int party_frame;             /* $7b */
static int move_phase;              /* $ab bit 0 analogue */

static void sprite_load(int sheet, int pal)
{
    char p[512];
    uint8_t *raw;
    size_t rawlen = 0;
    int nt, t;

    snprintf(p, sizeof p, "data/gfx/gfx/mapSpriteGraphics[%d].bin", sheet);
    raw = read_file(p, &rawlen);
    if (!raw) {
        fprintf(stderr, "[sprite] missing %s\n", p);
        return;
    }
    /* the rip stores these sheets already expanded: one palette index
     * per byte, logical (screen) order, 64 bytes per 8x8 tile - so
     * logical tile T is at T*64 (the old engine's sprite_blit rule) */
    if (rawlen % 64) {
        fprintf(stderr, "[sprite] %s: bad size %lu\n", p,
                (unsigned long)rawlen);
        free(raw);
        return;
    }
    nt = (int)(rawlen / 64);
    free(spr_sheet);
    spr_sheet = raw;                 /* take ownership */
    spr_sheet_len = nt;
    (void)t;

    snprintf(p, sizeof p, "data/gfx/gfx/npcPalettes[%d].bin", pal);
    raw = read_file(p, NULL);
    if (raw) {
        memcpy(spr_rgba, raw, 8 * 4);
        free(raw);
    } else {
        fprintf(stderr, "[sprite] missing %s\n", p);
        return;
    }
    spr_loaded = 1;
}

/* data/map_sprite_slots.bin: 384 maps x 4 slots x (sheet, pal) */
static int sm_slot_sheet[4], sm_slot_pal[4];

static void load_sprite_slots(void)
{
    uint8_t *d = read_file("data/map_sprite_slots.bin", NULL);
    int k;

    if (!d)
        return;
    for (k = 0; k < 4; ++k) {
        sm_slot_sheet[k] = d[0 * 8 + k * 2];
        sm_slot_pal[k]   = d[0 * 8 + k * 2 + 1];
    }
    free(d);
}

/* draw one 8x8 sprite tile at (x, y) scale z, mirroring horizontally
 * if flip.  Color 0 is transparent. */
static void spr_tile(SDL_Renderer *r, int tile, int flip,
                     int x, int y, double z)
{
    const uint8_t *t = spr_sheet + (size_t)tile * 64;
    int px, py;

    if (!spr_loaded || tile < 0 || tile >= spr_sheet_len)
        return;
    for (py = 0; py < 8; ++py)
        for (px = 0; px < 8; ++px) {
            int sx = flip ? 7 - px : px;
            int ci = t[py * 8 + px];

            if (!ci)
                continue;
            SDL_SetRenderDrawColor(r, spr_rgba[ci][0], spr_rgba[ci][1],
                                   spr_rgba[ci][2], 0xFF);
            SDL_RenderFillRect(r, &(SDL_Rect){ x + (int)(sx * z),
                                               y + (int)(py * z),
                                               (int)(z + 0.5),
                                               (int)(z + 0.5) });
        }
}

/* DrawPlayerSub @b284: frame index = facing*2 + step, the four
 * subtiles are laid out 2x2 (top pair = head, bottom pair = feet);
 * $06 = 1 while stepping (the 1px walk bob). */
static void draw_actor(SDL_Renderer *r, int sx, int sy, double z)
{
    int frame = facing * 2 + (moving && (party_frame & 8) ? 1 : 0);
    int bob = (moving && !(move_phase & 1) && (party_frame & 8)) ? 1 : 0;
    int k;

    if (frame < 0 || frame > 7)
        frame = 0;
    sx -= (int)(bob * z);
    sy -= (int)(bob * z);
    for (k = 0; k < 4; ++k) {
        int tile = player_sprite_tiles[frame][k * 2];
        int flip = (player_sprite_tiles[frame][k * 2 + 1] & 0x40) ? 1 : 0;

        spr_tile(r, tile, flip,
                 sx + (int)((k % 2) * 8 * z), sy + (int)((k / 2) * 8 * z), z);
    }
}

/* ---------------------- the world viewport ------------------------ */

/* compose the viewport from .idx + work_pal (the pre-pack path; the
 * A/B reference for wp_pass).  The .idx file is the composed world
 * ALREADY assign-shifted - its bytes are final palette indices, the
 * same values wp_pass computes as (raw art + assign) % 64 - so a
 * pixel maps directly: ci = idx[px].  Proven against the reference
 * world PNGs by tools/ff4data.py --verify. */
static void compose_view(World *w, int cx, int cy, uint32_t *out,
                          int ou, int ov)
{
    int x, y;

    for (y = 0; y < ov; ++y) {
        int sy = cy + y;

        if (sy < 0 || sy >= w->idx_h) {
            for (x = 0; x < ou; ++x)
                out[(size_t)y * ou + x] = 0xFF000000u;
            continue;
        }
        for (x = 0; x < ou; ++x) {
            int sx = cx + x;
            uint8_t ci;

            if (sx < 0 || sx >= w->idx_w)
                ci = 0;
            else
                ci = w->idx[(size_t)sy * w->idx_w + sx];
            out[(size_t)y * ou + x] =
                0xFF000000u | ((uint32_t)w->work_pal[ci * 4 + 0] << 16) |
                ((uint32_t)w->work_pal[ci * 4 + 1] << 8) |
                w->work_pal[ci * 4 + 2];
        }
    }
}

/* whole-map view (T): the composed PNG scaled to fit the window */
static void draw_whole_map(SDL_Renderer *r)
{
    SDL_Texture *tex;
    double s = (win_w < (double)win_h * W->img.w / W->img.h)
                   ? (double)win_w / W->img.w
                   : (double)win_h / W->img.h;
    int dw = (int)(W->img.w * s), dh = (int)(W->img.h * s);

    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STATIC, W->img.w, W->img.h);
    SDL_UpdateTexture(tex, NULL, W->img.pixels, (size_t)W->img.w * 4);
    SDL_RenderCopy(r, tex, NULL,
                  &(SDL_Rect){ (win_w - dw) / 2, (win_h - dh) / 2, dw, dh });
    SDL_DestroyTexture(tex);
}

/* ------------------- the [wtile] cell reporter --------------------- */
/* V/B tile reporter (data/world/water_tiles.txt).  The batch hook
 * FF4_REPORT_CELL="<world> x,y x,y ..." replays recorded sessions
 * (verify_world_report.py); interactive V reports the cursor cell.  */

/* water band (data/world/water_tiles.txt): the same base tiles in
 * every world - 0x80..0x9f plus foam 0xac/0xad. */
static int world_base_is_water(int bt)
{
    return (bt >= 0x80 && bt <= 0x9F) || bt == 0xAC || bt == 0xAD;
}

/* engine-defined cell fingerprint: FNV-1a over the 16x16 .idx block */
static uint32_t world_cell_fp(World *w, int cx, int cy)
{
    uint32_t h = 2166136261u;
    int y, x;

    for (y = 0; y < 16; ++y) {
        const uint8_t *row = w->idx + (size_t)(cy * 16 + y) * w->idx_w + cx * 16;

        for (x = 0; x < 16; ++x) {
            h ^= row[x];
            h *= 16777619u;
        }
    }
    return h;
}

static int tile_report;      /* V mode on */
static int cur_cx, cur_cy;   /* the report cursor, in cells */

static void report_world_cell(FILE *log, int cx, int cy)
{
    World *w = W;
    int comp = w->tiles[(size_t)cy * w->cells + cx];
    int b[4], nw = 0, k;
    char base_txt[128];
    uint32_t fp;
    int nx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    int ny[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

    for (k = 0; k < 4; ++k) {
        b[k] = w->base[comp * 4 + k];
        if (world_base_is_water(b[k]))
            ++nw;
    }
    fp = world_cell_fp(w, cx, cy);
    snprintf(base_txt, sizeof base_txt,
             "%d(0x%02X%s),%d(0x%02X%s),%d(0x%02X%s),%d(0x%02X%s)",
             b[0], b[0], world_base_is_water(b[0]) ? "W" : "",
             b[1], b[1], world_base_is_water(b[1]) ? "W" : "",
             b[2], b[2], world_base_is_water(b[2]) ? "W" : "",
             b[3], b[3], world_base_is_water(b[3]) ? "W" : "");

    /* one-line echo on stdout (verify_world_report greps this) */
    printf("[wtile] cell=(%d,%d) composed=%d (0x%02X) base=%s water=%d/4\n",
           cx, cy, comp, comp, base_txt, nw);

    /* full block: stdout + FF4_CELLFILE log */
    printf("[wtile] world=%d (%s) at (%d,%d) composed=%d (0x%02X)\n",
           cur, w->name, cx, cy, comp, comp);
    if (log) {
        fprintf(log, "[wtile] world=%d (%s) at (%d,%d) composed=%d (0x%02X)\n",
                cur, w->name, cx, cy, comp, comp);
        fprintf(log, "  base=%s water=%d/4 fp=%08X\n", base_txt, nw, fp);
        fprintf(log, "  neighbours:");
        for (k = 0; k < 8; ++k) {
            int x = cx + nx[k], y = cy + ny[k];

            if (x < 0 || y < 0 || x >= w->cells || y >= w->cells)
                fprintf(log, " -1");
            else
                fprintf(log, " %d", w->tiles[(size_t)y * w->cells + x]);
        }
        fprintf(log, "\n");
        fprintf(log, "  w%d %d %d 0x%02X %d %d %d %d %08X\n",
                cur, cx, cy, comp, b[0], b[1], b[2], b[3], fp);
    }
}

/* FF4_REPORT_CELL batch: "<world> x,y x,y ..." - report every cell,
 * log to FF4_CELLFILE (default ff4_cell.txt), exit.  Returns 1 if the
 * env var was present and the batch ran. */
static int run_report_env(void)
{
    const char *spec = getenv("FF4_REPORT_CELL");
    const char *file = getenv("FF4_CELLFILE");
    char buf[4096], *tok;
    FILE *log;

    if (!spec || !*spec)
        return 0;
    snprintf(buf, sizeof buf, "%s", spec);
    tok = strtok(buf, " \t");
    if (!tok)
        return 0;
    if (load_world(atoi(tok)) != 0) {
        fprintf(stderr, "[wtile] bad world '%s'\n", tok);
        return 1;
    }
    log = fopen(file ? file : "ff4_cell.txt", "w");
    while ((tok = strtok(NULL, " \t,")) != NULL) {
        int cx = atoi(tok);
        int cy;

        tok = strtok(NULL, " \t,");
        if (!tok)
            break;
        cy = atoi(tok);
        if (cx < 0 || cy < 0 || cx >= W->cells || cy >= W->cells) {
            printf("[wtile] cell=(%d,%d) out of range\n", cx, cy);
            continue;
        }
        report_world_cell(log, cx, cy);
    }
    if (log)
        fclose(log);
    printf("[wtile] batch report written to %s\n",
           file ? file : "ff4_cell.txt");
    return 1;
}

/* B key: bulk-report one representative cell per distinct composed
 * tile on the map. */
static void report_distinct_cells(FILE *log)
{
    uint8_t seen[256];
    int x, y;

    memset(seen, 0, sizeof seen);
    for (y = 0; y < W->cells; ++y)
        for (x = 0; x < W->cells; ++x) {
            int comp = W->tiles[(size_t)y * W->cells + x];

            if (seen[comp])
                continue;
            seen[comp] = 1;
            report_world_cell(log, x, y);
        }
}

/* --------------------------- shots -------------------------------- */

static void save_shot(SDL_Renderer *r)
{
    const char *out = getenv("FF4_SHOT");
    SDL_Surface *sf;
    uint32_t *px = NULL;
    int pitch;

    if (!out)
        return;
    sf = SDL_CreateRGBSurfaceWithFormat(0, win_w, win_h, 32,
                                        SDL_PIXELFORMAT_ABGR8888);
    if (!sf)
        return;
    /* ff4_png_write emits the buffer bytes as PNG RGBA, so read back in
     * ABGR8888 - memory order R,G,B,A - or the channels swap */
    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ABGR8888, sf->pixels,
                         sf->pitch);
    pitch = sf->pitch;
    if (pitch == win_w * 4)
        ff4_png_write(out, win_w, win_h, (const uint32_t *)sf->pixels);
    else {
        int y;

        px = (uint32_t *)malloc((size_t)win_w * win_h * 4);
        for (y = 0; y < win_h; ++y)
            memcpy(px + (size_t)y * win_w,
                   (uint8_t *)sf->pixels + (size_t)y * pitch, win_w * 4);
        ff4_png_write(out, win_w, win_h, px);
        free(px);
    }
    SDL_FreeSurface(sf);
    printf("[shot] %s (%dx%d)\n", out, win_w, win_h);
}

/* ---------------------------- main -------------------------------- */

int main(int argc, char **argv)
{
    const char *e;
    int world_env = -1, shot_frame = -1, shot_taken = 0;
    double zoom_env = 1.0;
    int whole_map = 0, quit = 0;
    SDL_Window *win = NULL;
    uint32_t *vbuf = NULL;
    SDL_Texture *vtex = NULL;
    FILE *blog = NULL;

    (void)argc; (void)argv;

    if ((e = getenv("FF4_NOCLIP")) && atoi(e))
        noclip = 1;
    if ((e = getenv("FF4_WORLD")))
        world_env = atoi(e);
    if ((e = getenv("FF4_ZOOM")))
        zoom_env = atof(e);
    if ((e = getenv("FF4_MAPX")))
        ax = atoi(e) * 16 + 8;
    if ((e = getenv("FF4_MAPY")))
        ay = atoi(e) * 16 + 8;
    if ((e = getenv("FF4_LAVASTEP")) && *e)
        lava_step_env = atoi(e);
    if ((e = getenv("FF4_SHOT"))) {
        const char *d = getenv("FF4_SHOT_DELAY");

        shot_frame = d ? atoi(d) : 0;
    }

    /* the batch reporter runs headless and exits */
    if (run_report_env())
        return 0;

    /* sub-maps return in recovery stage 2 */
    if (getenv("FF4_WARPTO"))
        fprintf(stderr, "[stage1] sub-maps return in recovery stage 2 "
                        "(world view only for now)\n");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_CreateWindowAndRenderer(WIN_DEFAULT_W, WIN_DEFAULT_H,
                                    SDL_WINDOW_RESIZABLE, &win, &ren) != 0) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetWindowSize(win, &win_w, &win_h);

    if (load_world(world_env >= 0 ? world_env : 0) != 0) {
        fprintf(stderr, "[world] load failed\n");
        return 1;
    }
    load_sprite_slots();
    sprite_load(sm_slot_sheet[0], sm_slot_pal[0]);
    printf("[world] %s: %dx%d px (%dx%d cells), pack %s\n",
           W->name, W->idx_w, W->idx_h, W->cells, W->cells,
           wp_loaded ? "loaded" : "off");

    vw = 256; vh = 224;                    /* SNES logical viewport */
    zoom = zoom_env;
    vbuf = (uint32_t *)malloc((size_t)vw * vh * 4);
    update_camera();

    while (!quit) {
        SDL_Event ev;
        const Uint8 *keys;
        double dx = 0, dy = 0;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                quit = 1;
            else if (ev.type == SDL_WINDOWEVENT &&
                     ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;

                if (k == SDLK_ESCAPE || k == SDLK_q)
                    quit = 1;
                else if (k == SDLK_t)
                    whole_map = !whole_map;
                else if (k == SDLK_v) {
                    tile_report = !tile_report;
                    if (tile_report) {
                        cur_cx = (int)ax / 16;
                        cur_cy = (int)ay / 16;
                        printf("[wtile] report mode ON, cursor at "
                               "(%d,%d)\n", cur_cx, cur_cy);
                    } else
                        printf("[wtile] report mode OFF\n");
                } else if (k == SDLK_b && W->tiles) {
                    blog = fopen(getenv("FF4_CELLFILE")
                                 ? getenv("FF4_CELLFILE") : "ff4_cell.txt",
                                 "a");
                    report_distinct_cells(blog);
                    if (blog) { fclose(blog); blog = NULL; }
                } else if (k == SDLK_1 || k == SDLK_2 || k == SDLK_3) {
                    if (load_world(k - SDLK_1) == 0) {
                        sprite_load(sm_slot_sheet[0], sm_slot_pal[0]);
                        update_camera();
                    }
                } else if (k == SDLK_r) {
                    ax = 15 * 16 + 8; ay = 16 * 16 + 8;
                    update_camera();
                } else if (k == SDLK_KP_PLUS || k == SDLK_EQUALS) {
                    if (zoom < 8) zoom += zoom >= 1 ? 1 : 0.25;
                } else if (k == SDLK_KP_MINUS || k == SDLK_MINUS) {
                    if (zoom > 0.25) zoom -= zoom >= 1.25 ? 1 : 0.25;
                }
            }
        }

        keys = SDL_GetKeyboardState(NULL);
        if (tile_report) {
            /* the cursor takes the arrows */
            if (keys[SDL_SCANCODE_LEFT])  --cur_cx;
            if (keys[SDL_SCANCODE_RIGHT]) ++cur_cx;
            if (keys[SDL_SCANCODE_UP])    --cur_cy;
            if (keys[SDL_SCANCODE_DOWN])  ++cur_cy;
            if (cur_cx < 0) cur_cx = 0;
            if (cur_cy < 0) cur_cy = 0;
            if (cur_cx >= W->cells) cur_cx = W->cells - 1;
            if (cur_cy >= W->cells) cur_cy = W->cells - 1;
        } else {
            if (keys[SDL_SCANCODE_LEFT])  { dx = -1; facing = 3; }
            if (keys[SDL_SCANCODE_RIGHT]) { dx = 1;  facing = 1; }
            if (keys[SDL_SCANCODE_UP])   { dy = -1; facing = 0; }
            if (keys[SDL_SCANCODE_DOWN]) { dy = 1;  facing = 2; }
            moving = (dx || dy) ? 1 : 0;
            if (moving) {
                int nx = (int)(ax + dx), ny = (int)(ay + dy);
                int cellx = (nx + (dx > 0 ? 7 : dx < 0 ? -8 : 0)) / 16;
                int celly = (ny + (dy > 0 ? 7 : dy < 0 ? -8 : 0)) / 16;

                if (world_passable(cellx, (int)ay / 16) &&
                    world_passable((int)ax / 16, celly) &&
                    world_passable(cellx, celly)) {
                    ax += dx; ay += dy;
                    ++move_phase;
                }
            }
            ++party_frame;            /* $7b free-run */
        }

        /* lava cadence: one LavaPalTbl step every 16 frames */
        if (W->lavapal) {
            int step = lava_step_env >= 0
                         ? lava_step_env
                         : (party_frame >> 4) & 7;

            world_patch_palette(W, step);
            lava_sync_pack(W);
        }

        update_camera();

        /* ---- draw ---- */
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xFF);
        SDL_RenderClear(ren);

        if (whole_map) {
            draw_whole_map(ren);
        } else {
            if (wp_loaded)
                wp_pass(vbuf, vw, vh);
            else
                compose_view(W, (int)cam_x, (int)cam_y, vbuf, vw, vh);

            if (!vtex)
                vtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         vw, vh);
            SDL_UpdateTexture(vtex, NULL, vbuf, vw * 4);
            {
                int dw = (int)(vw * zoom), dh = (int)(vh * zoom);

                SDL_RenderCopy(ren, vtex, NULL,
                               &(SDL_Rect){ (win_w - dw) / 2,
                                            (win_h - dh) / 2, dw, dh });
            }
            /* the actor, centred on its world position */
            if (!getenv("FF4_NOACTOR")) {
                int sx = (int)(ax - cam_x) - 8;
                int sy = (int)(ay - cam_y) - 8;
                int ox = (win_w - (int)(vw * zoom)) / 2;
                int oy = (win_h - (int)(vh * zoom)) / 2;

                draw_actor(ren, ox + (int)(sx * zoom), oy + (int)(sy * zoom),
                           zoom);
            }
            if (tile_report) {
                /* the yellow report cursor */
                int ox = (win_w - (int)(vw * zoom)) / 2;
                int oy = (win_h - (int)(vh * zoom)) / 2;
                int x = ox + (int)((cur_cx * 16 - cam_x) * zoom);
                int y = oy + (int)((cur_cy * 16 - cam_y) * zoom);
                int s = (int)(16 * zoom);
                SDL_Rect rc = { x, y, s, s };

                SDL_SetRenderDrawColor(ren, 0xFF, 0xE0, 0x00, 0xFF);
                SDL_RenderDrawRect(ren, &rc);
            }
        }
        SDL_RenderPresent(ren);

        ++frame_counter;
        if (shot_frame >= 0 && !shot_taken &&
            (int)frame_counter > shot_frame) {
            save_shot(ren);
            shot_taken = 1;
            quit = 1;              /* the shot harness is bounded */
        }
        SDL_Delay(16);            /* ~60 fps */
    }

    free(vbuf);
    if (vtex) SDL_DestroyTexture(vtex);
    free(spr_sheet);
    wp_free();
    SDL_Quit();
    return 0;
}
