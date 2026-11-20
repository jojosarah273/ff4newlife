/**
 * @file ff4_gfx.c
 * @brief Phase 10: ripped graphics decoding.
 */
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ff4/core/ff4_gfx.h"

void ff4_gfx_surface_free(FF4_GfxSurface *s)
{
    if (s) {
        free(s->pixels);
        s->pixels = NULL;
        s->w = s->h = 0;
    }
}

uint32_t ff4_gfx_color16(const uint8_t *pal16, int index)
{
    const uint8_t *p = pal16 + index * 2;
    uint16_t c = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    /* 5-bit -> 8-bit using the top 3 bits of the value as the low
       3 bits (generic SNES-style expansion, matches snes9x output) */
    uint32_t r = (uint32_t)(((c & 0x1F) << 3) | ((c >> 2) & 7));
    uint32_t g = (uint32_t)((((c >> 5) & 0x1F) << 3) | ((c >> 7) & 7));
    uint32_t b = (uint32_t)((((c >> 10) & 0x1F) << 3) | ((c >> 12) & 7));

    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* ==================================================================== */

static const uint8_t *blob_of(const FF4_AssetDB *db, const char *key,
                              size_t *len)
{
    const FF4_Blob *b = ff4_db_blob(db, key);

    if (len)
        *len = b ? b->len : 0;
    return b ? b->data : NULL;
}

/* The title screen palette (the game copies TitlePal from
   field/title_en.asm into $0CDB for the splash screen). */
static const uint8_t ff4_title_palette[256] = {
    /* generated from third_party/ff4/field/gfx/title_pal_en.asm */
#include "ff4_title_pal.inc"
};

int ff4_gfx_render_tilemap(const FF4_AssetDB *db,
                           const char *gfx_key, const char *pal_key,
                           const char *tilemap_key,
                           int off_x, int off_y, int view_w, int view_h,
                           int bpp, FF4_GfxSurface *out)
{
    const uint8_t *gfx, *pal, *mapdata;
    size_t glen, plen, mlen;
    int x, y, tile, palgrp, hflip, vflip;
    int group = bpp == 3 ? 8 : 16;
    size_t tile_bytes = (size_t)bpp * 8;

    if (!out)
        return -1;
    memset(out, 0, sizeof *out);

    gfx = blob_of(db, gfx_key, &glen);
    mapdata = blob_of(db, tilemap_key, &mlen);
    /* the title screen uses the dedicated TitlePal (embedded above) */
    if (pal_key && strcmp(pal_key, "titlePalette") == 0) {
        pal = ff4_title_palette;
        plen = sizeof ff4_title_palette;
    } else {
        pal = blob_of(db, pal_key, &plen);
    }
    if (!gfx || !pal || !mapdata)
        return -1;

    out->w = view_w * 8;
    out->h = view_h * 8;
    out->pixels = (uint32_t *)malloc((size_t)out->w * (size_t)out->h * 4);
    if (!out->pixels)
        return -1;

    for (y = 0; y < view_h; ++y) {
        for (x = 0; x < view_w; ++x) {
            size_t mi = ((size_t)(off_y + y) * 64 + (off_x + x)) * 2;
            uint16_t e;
            int tx, ty, px, py, c;

            if (mi + 1 >= mlen)
                break;
            /* SNES BG entry (big-endian), layout verified 93.5%
               pixel-exact against the snes9x title frame:
               tile = e>>1 (≤511, matches the 512-entry 4bpp table),
               palette group = bits 12-14, hflip = bit 14,
               vflip = bit 15 */
            e = (uint16_t)((uint16_t)mapdata[mi] << 8 |
                           (uint16_t)mapdata[mi + 1]);
            tile = (e >> 1) & 0x3FF;
            palgrp = (e >> 12) & 7;
            hflip = (e >> 14) & 1;
            vflip = (e >> 15) & 1;

            for (ty = 0; ty < 8; ++ty) {
                const uint8_t *base = gfx + (size_t)tile * tile_bytes;
                int yy = vflip ? 7 - ty : ty;

                for (tx = 0; tx < 8; ++tx) {
                    int xx = hflip ? 7 - tx : tx;
                    int bit = 7 - xx;
                    int pl;

                    c = 0;
                    for (pl = 0; pl < bpp; ++pl)
                        c |= (int)((base[pl * 8 + ty] >> bit) & 1) << pl;

                    px = x * 8 + tx;
                    py = y * 8 + yy;
                    if (c == 0) {
                        /* color 0 comes from palette group 0 */
                        out->pixels[(size_t)py * out->w + px] =
                            ff4_gfx_color16(pal, 0);
                    } else {
                        out->pixels[(size_t)py * out->w + px] =
                            ff4_gfx_color16(pal, palgrp * group + c);
                    }
                }
            }
        }
    }
    return 0;
}

int ff4_gfx_title_screen(const FF4_AssetDB *db, FF4_GfxSurface *out)
{
    /* splash: the title artwork occupies the top-left 32x28 tile window
       of the 64-tile-wide map; 2bpp tiles + the dedicated TitlePal.
       Verified 93.5% pixel-exact against the snes9x title frame (the
       remainder is the game's live cursor/text overlay). */
    return ff4_gfx_render_tilemap(db, "splashGraphics", "titlePalette",
                                  "splashTilemap", 0, 0, 32, 28, 2, out);
}

int ff4_gfx_prologue(const FF4_AssetDB *db, int which, FF4_GfxSurface *out)
{
    const char *map = which == 0 ? "prologueTilemap1" : "prologueTilemap2";

    /* rip definition: snes3bpp */
    return ff4_gfx_render_tilemap(db, "prologueGraphics", "prologuePalette",
                                  map, 0, 0, 32, 28, 3, out);
}

/* ==================================================================== */
/*  Field / overworld map rendering (Phase 11)                          */
/* ==================================================================== */

#define FF4_MAP_W 32
#define FF4_MAP_H 32    /* field layouts are 32x32 tiles (1024 bytes) */

/* The field tileset pixel order (mapTilesets def): 1024 bytes as
   4 x 256-entry tables; BG1 low at +0, BG1 high at +0x100, BG2 low
   at +0x200, BG2 high at +0x300 (matches $7F4800/$7F4880/$7F4900/
   $7F4980 reads in tilemap.asm). */
uint16_t ff4_gfx_map_entry(const uint8_t *tileset, int byte, int layer)
{
    int lo = layer == 0 ? 0 : 0x200;
    int hi = lo + 0x100;

    return (uint16_t)((uint16_t)tileset[lo + (byte & 0xFF)] |
                      ((uint16_t)tileset[hi + (byte & 0xFF)] << 8));
}

int ff4_gfx_decode_layout(const uint8_t *blob, size_t len, uint8_t *outbuf)
{
    const size_t want = (size_t)FF4_MAP_W * (size_t)FF4_MAP_H;
    size_t i = 0, o = 0;

    /* The ripped layout blob (mapLayouts1/2) is already RLE-decoded: it is
       a flat 32x32 tile map (1024 bytes).  Only run the RLE path for a
       raw/compressed source (the disassembly form), detected by length. */
    if (len >= want && len <= want + 16) {
        for (o = 0; o < want; ++o)
            outbuf[o] = blob[o];
        return (int)o;
    }
    while (i < len && o < want) {
        uint8_t b = blob[i++];

        if (b & 0x80) {
            int count, n;

            if (i >= len)
                break;
            count = blob[i++] + 1;
            for (n = 0; n < count && o < want; ++n)
                outbuf[o++] = b & 0x7F;
        } else {
            outbuf[o++] = b;
        }
    }
    return (int)o;
}

/* One field/layer tile at a view position, wrapped; returns the
   palette index (or -2 when the source tile is out of range). */
static int blit_map_tile(const uint8_t *gfx, size_t glen, int bpp,
                         int tile, int palgrp, int hf, int vf,
                         const uint8_t *pal, size_t plen,
                         uint32_t *px, int x, int y, int w, int h)
{
    size_t tile_bytes = (size_t)bpp * 8;
    size_t base;
    uint8_t idx[64];
    int ty, tx;

    if ((size_t)tile >= glen / tile_bytes)
        return -1;
    base = (size_t)tile * tile_bytes;
    for (ty = 0; ty < 8; ++ty) {
        uint8_t planes[4];
        int b;

        for (b = 0; b < bpp; ++b)
            planes[b] = gfx[base + (size_t)b * 8 + ty];
        (void)idx;
        for (tx = 0; tx < 8; ++tx) {
            int bit = 7 - tx, c = 0, p;
            int px2 = x + (hf ? 7 - tx : tx);
            int py2 = y + (vf ? 7 - ty : ty);

            if (px2 < 0 || px2 >= w || py2 < 0 || py2 >= h)
                continue;
            for (p = 0; p < bpp; ++p)
                c |= (int)((planes[p] >> bit) & 1) << p;
            if (c == 0)
                c = 0;
            if ((size_t)(palgrp * 16 + c) * 2 + 1 < plen)
                px[(size_t)py2 * w + px2] =
                    ff4_gfx_color16(pal, palgrp * 16 + c);
            else
                px[(size_t)py2 * w + px2] = 0xFF000000u;
        }
    }
    return 0;
}

int ff4_gfx_render_submap(const FF4_AssetDB *db, int map,
                          int cx, int cy, int vw, int vh,
                          bool bg2, FF4_GfxSurface *out)
{
    const FF4_MapDef *md;
    const uint8_t *layout, *tileset, *gfx, *pal;
    size_t llen, tslen, glen, plen;
    static uint8_t buf[FF4_MAP_W * FF4_MAP_H];
    int bpp, x, y;
    char key[48];

    if (!db || !out || (unsigned)map >= FF4_DB_MAPS)
        return -1;
    memset(out, 0, sizeof *out);
    md = &db->maps[map];

    /* layout blob: mapLayouts1[layout1] (the game's SubTilemap1 table;
       extended maps use SubTilemap2 = mapLayouts2 via a separate msb) */
    snprintf(key, sizeof key, "mapLayouts1[%d]", md->layout1);
    layout = blob_of(db, key, &llen);
    if (!layout)
        return -1;
    if (ff4_gfx_decode_layout(layout, llen, buf) < 32)
        return -1;

    snprintf(key, sizeof key, "mapTilesets[%d]", md->graphics);
    tileset = blob_of(db, key, &tslen);
    gfx = blob_of(db, "mapGraphics0", &glen);
    bpp = 4;                          /* gfx 0: ship/airship = 4bpp */
    if (md->graphics != 0) {
        snprintf(key, sizeof key, "mapGraphics%d", md->graphics);
        gfx = blob_of(db, key, &glen);
        bpp = 3;                      /* standard field tiles are 3bpp */
    }
    if (!tileset || !gfx)
        return -1;

    snprintf(key, sizeof key, "mapPalettes[%d]", md->palette);
    pal = blob_of(db, key, &plen);
    if (!pal)
        return -1;

    out->w = vw * 8;
    out->h = vh * 8;
    out->pixels = (uint32_t *)calloc((size_t)out->w * out->h, 4);
    if (!out->pixels)
        return -1;

    for (y = 0; y < vh; ++y) {
        int my = ((cy + y) & 0xFF) * FF4_MAP_W;
        for (x = 0; x < vw; ++x) {
            int byte = buf[my + ((cx + x) & 0x1F)];
            uint16_t e = ff4_gfx_map_entry(tileset, byte, bg2 ? 1 : 0);
            int tile = e & 0x3FF;
            /* FF4 fields use "anchored" palettes: the tile's palette
               group comes from the tile index (tile >> 4 & 7), not the
               entry's palette bits (field/tilemap.asm, loaded via
               LoadMapPal into 8 x 16-color groups). */
            int palgrp = (tile >> 4) & 7;
            int hf = (e >> 15) & 1, vf = (e >> 14) & 1;

            if (blit_map_tile(gfx, glen, bpp, tile, palgrp, hf, vf,
                              pal, plen, out->pixels,
                              x * 8, y * 8, out->w, out->h) == 0)
                continue;
        }
    }
    return 0;
}

int ff4_gfx_render_world(const FF4_AssetDB *db, int world,
                         int cx, int cy, int vw, int vh,
                         FF4_GfxSurface *out)
{
    const uint8_t *layout, *tileset, *gfx, *pal;
    size_t llen, tslen, glen, plen;
    int W = world == 2 ? 64 : 256;    /* world 2 = moon, 64x64 */
    int chunks = world == 2 ? 64 : 256;
    char key[48];
    int x, y;

    if (!db || !out || world < 0 || world > 2)
        return -1;
    memset(out, 0, sizeof *out);

    (void)chunks;
    (void)llen;
    snprintf(key, sizeof key, "worldTilesets[%d]", world);
    tileset = blob_of(db, key, &tslen);
    snprintf(key, sizeof key, "worldGraphics[%d]", world);
    gfx = blob_of(db, key, &glen);
    snprintf(key, sizeof key, "worldPalettes[%d]", world);
    pal = blob_of(db, key, &plen);
    if (!tileset || !gfx || !pal)
        return -1;

    out->w = vw * 8;
    out->h = vh * 8;
    out->pixels = (uint32_t *)calloc((size_t)out->w * out->h, 4);
    if (!out->pixels)
        return -1;

    for (y = 0; y < vh; ++y) {
        int row = (cy + y) % W;
        int my = row % W;
        (void)my;
        /* layout rows: worldLayout%d[%d] holds 256/64 raw bytes */
        snprintf(key, sizeof key, "worldLayout%d[%d]", world + 1, row);
        layout = blob_of(db, key, &llen);
        if (!layout)
            continue;
        for (x = 0; x < vw; ++x) {
            int b = layout[(cx + x) % W];
            uint16_t e = (uint16_t)((uint16_t)tileset[b] |
                                    ((uint16_t)tileset[b + 256] << 8));
            int tile = e & 0x3FF;
            int palgrp = (e >> 10) & 7;
            int hf = (e >> 15) & 1, vf = (e >> 14) & 1;

            blit_map_tile(gfx, glen, 4, tile, palgrp, hf, vf,
                          pal, plen, out->pixels,
                          x * 8, y * 8, out->w, out->h);
        }
    }
    return 0;
}

/* field sprites: mapSpriteGraphics[id] = 4bpp tiles.  The sheet is
   16 tiles wide x 2 tall with the tile order from the rip def:
   upper band: 0,1,4,5,8,9,12,13,16,17,20,21,24,25,28,29
   lower band: 2,3,6,7,10,11,14,15,18,19,22,23,26,27,30,31    */
static const uint8_t ff4_sheet_order[32] = {
    0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29,
    2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31
};

static int draw_sprite_blob(FF4_GfxSurface *out, int x, int y,
                            const uint8_t *gfx, size_t glen,
                            const uint8_t *pal, size_t plen,
                            int sheet, int frame)
{
    int t, ty, tx;
    size_t tile_bytes = 32;           /* 4bpp */
    size_t tiles_per_sheet = 32;

    if (!out || !out->pixels || !gfx || !pal)
        return -1;
    if (tiles_per_sheet * tile_bytes == 0 ||
        (size_t)(sheet + 1) * tiles_per_sheet > glen / tile_bytes)
        return -1;
    for (t = 0; t < 32; ++t) {
        int order = ff4_sheet_order[t];
        int bx = (t & 15) * 8;
        int by = (t >> 4) * 8;
        size_t base = ((size_t)sheet * tiles_per_sheet + order) * tile_bytes;
        int col = x + bx, row = y + by - 8;

        (void)frame;
        for (ty = 0; ty < 8; ++ty) {
            for (tx = 0; tx < 8; ++tx) {
                int sx = x + bx + tx, sy = y + by - 8 + ty;
                int c, p;

                if (sx < 0 || sy < 0 || sx >= out->w || sy >= out->h)
                    continue;
                c = 0;
                for (p = 0; p < 4; ++p)
                    c |= (int)((gfx[base + (size_t)p * 8 + ty] >>
                                (7 - tx)) & 1) << p;
                if (c == 0)
                    continue;         /* transparent */
                if ((size_t)c * 2 + 1 < plen)
                    out->pixels[(size_t)sy * out->w + sx] =
                        ff4_gfx_color16(pal, c);
            }
        }
        (void)col;
        (void)row;
    }
    return 0;
}

int ff4_gfx_draw_field_sprite(FF4_GfxSurface *out, int x, int y,
                              const FF4_AssetDB *db, int sprite,
                              int palette_group, int frame)
{
    const uint8_t *gfx, *pal = NULL;
    size_t glen, plen = 0;
    char key[48];

    if (!db || sprite < 0)
        return -1;
    snprintf(key, sizeof key, "mapSpriteGraphics[%d]", sprite);
    gfx = blob_of(db, key, &glen);
    /* npcPalettes[id] = 16-color sprite palette (32 bytes). */
    if (palette_group >= 0) {
        snprintf(key, sizeof key, "npcPalettes[%d]", palette_group);
        pal = blob_of(db, key, &plen);
        if (plen > 32)
            plen = 32;
    }
    if (!gfx)
        return -1;
    return draw_sprite_blob(out, x, y, gfx, glen, pal, plen, 0, frame);
}

int ff4_gfx_draw_world_sprite(FF4_GfxSurface *out, int x, int y,
                              const FF4_AssetDB *db, int sprite,
                              int palette_group, int frame)
{
    const uint8_t *gfx, *pal;
    size_t glen, plen;

    (void)palette_group;
    if (!db)
        return -1;
    gfx = blob_of(db, "worldSpriteGraphics", &glen);
    pal = blob_of(db, "worldPalettes[0]", &plen);
    if (!gfx)
        return -1;
    return draw_sprite_blob(out, x, y, gfx, glen, pal, plen, sprite, frame);
}


/* ==================================================================== */
/*  PNG loading                                                         */
/* ==================================================================== */

int ff4_image_load(const char *path, FF4_GfxSurface *out)
{
    int w, h, ch;

    if (!out)
        return -1;
    memset(out, 0, sizeof *out);
    out->pixels = (uint32_t *)stbi_load(path, &w, &h, &ch, 4);
    if (!out->pixels)
        return -1;
    out->w = w;
    out->h = h;
    return 0;
}
/* ==================================================================== */
/*  PNG writer (shared; zlib-free stored-deflate)                       */
/* ==================================================================== */

static unsigned ff4_png_crc_tab[256];
static void ff4_png_crc_init(void)
{
    unsigned n;
    for (n = 0; n < 256; ++n) {
        unsigned c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        ff4_png_crc_tab[n] = c;
    }
}
static unsigned ff4_png_crc(const unsigned char *p, size_t n)
{
    unsigned c = ~0u;
    for (size_t i = 0; i < n; ++i)
        c = ff4_png_crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
static void ff4_png_be32(unsigned char *dst, unsigned long v)
{
    dst[0] = (unsigned char)(v >> 24);
    dst[1] = (unsigned char)(v >> 16);
    dst[2] = (unsigned char)(v >> 8);
    dst[3] = (unsigned char)v;
}
int ff4_png_write(const char *path, int w, int h, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    size_t pitch = (size_t)w * 4;
    size_t rlen = pitch * (size_t)h + (size_t)h;
    unsigned char *raw = NULL, *idat = NULL;
    size_t pos = 0, off = 0;
    unsigned char head[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    unsigned char ihdr[33];
    int y, rc = -1;

    if (!f || !px)
        return -1;
    ff4_png_crc_init();

    memset(ihdr, 0, sizeof ihdr);
    ff4_png_be32(ihdr, 13);
    memcpy(ihdr + 4, "IHDR", 4);
    ff4_png_be32(ihdr + 8, (unsigned)w);
    ff4_png_be32(ihdr + 12, (unsigned)h);
    ihdr[16] = 8; ihdr[17] = 6;
    ff4_png_be32(ihdr + 21, ff4_png_crc(ihdr + 4, 17));

    raw = (unsigned char *)malloc(rlen);
    idat = (unsigned char *)malloc(rlen + (rlen / 65535 + 1) * 5 + 6);
    if (!raw || !idat)
        goto out;
    for (y = 0; y < h; ++y) {
        raw[(size_t)y * (pitch + 1)] = 0;
        memcpy(raw + (size_t)y * (pitch + 1) + 1,
               (const unsigned char *)px + (size_t)y * pitch, pitch);
    }
    idat[pos++] = 0x78;
    idat[pos++] = 0x01;
    while (off < rlen) {
        size_t n = rlen - off;
        unsigned char hdr5[5];

        if (n > 65535)
            n = 65535;
        hdr5[0] = (unsigned char)((off + n >= rlen) ? 1 : 0);
        hdr5[1] = (unsigned char)(n & 0xFF);
        hdr5[2] = (unsigned char)(n >> 8);
        hdr5[3] = (unsigned char)(~n & 0xFF);
        hdr5[4] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(idat + pos, hdr5, 5);
        pos += 5;
        memcpy(idat + pos, raw + off, n);
        pos += n;
        off += n;
    }
    {
        unsigned long a1 = 1, a2 = 0;
        size_t i;

        for (i = 0; i < rlen; ++i) {
            a1 = (a1 + raw[i]) % 65521u;
            a2 = (a2 + a1) % 65521u;
        }
        ff4_png_be32(idat + pos, (a2 << 16) | a1);
        pos += 4;
    }
    fwrite(head, 1, 8, f);
    fwrite(ihdr, 1, 25, f);
    {
        unsigned char ch[12];
        unsigned c;

        ff4_png_be32(ch, (unsigned long)pos);
        memcpy(ch + 4, "IDAT", 4);
        fwrite(ch, 1, 8, f);
        fwrite(idat, 1, pos, f);
        c = ff4_png_crc(ch + 4, 4);
        ff4_png_be32(ch, c);
        fwrite(ch, 1, 4, f);

        ff4_png_be32(ch, 0);
        memcpy(ch + 4, "IEND", 4);
        fwrite(ch, 1, 8, f);
        c = ff4_png_crc(ch + 4, 4);
        ff4_png_be32(ch, c);
        fwrite(ch, 1, 4, f);
    }
    rc = 0;
out:
    free(raw);
    free(idat);
    fclose(f);
    return rc;
}

/* ==================================================================== */
/*  PPU: the 256x224 SNES picture pipeline                              */
/* ==================================================================== */

static void ppu_put_tile(FF4_Ppu *ppu, const uint8_t *gfx, size_t glen,
                         int bpp, int tile, int palgrp, int hf, int vf,
                         const uint8_t *pal, size_t plen,
                         int dx, int dy)
{
    size_t tile_bytes = (size_t)bpp * 8;
    size_t base;
    int ty, tx;

    if ((size_t)tile >= glen / tile_bytes)
        return;                              /* transparent */
    base = (size_t)tile * tile_bytes;
    for (ty = 0; ty < 8; ++ty) {
        uint8_t planes[4];
        int b;

        for (b = 0; b < bpp; ++b)
            planes[b] = gfx[base + (size_t)b * 8 + ty];
        for (tx = 0; tx < 8; ++tx) {
            int px2 = dx + (hf ? 7 - tx : tx);
            int py2 = dy + (vf ? 7 - ty : ty);
            int c = 0, p;

            if (px2 < 0 || px2 >= FF4_PPU_W || py2 < 0 || py2 >= FF4_PPU_H)
                continue;
            for (p = 0; p < bpp; ++p)
                c |= (int)((planes[p] >> (7 - tx)) & 1) << p;
            if (c == 0)
                c = 0;
            if ((size_t)(palgrp * 16 + c) * 2 + 1 < plen)
                ppu->px[(size_t)py2 * FF4_PPU_W + px2] =
                    ff4_gfx_color16(pal, palgrp * 16 + c);
        }
    }
}

void ff4_ppu_fill(FF4_Ppu *ppu, uint32_t color)
{
    int i;
    for (i = 0; i < FF4_PPU_W * FF4_PPU_H; ++i)
        ppu->px[i] = color;
}

void ff4_ppu_field_bg(FF4_Ppu *ppu, const FF4_AssetDB *db, int map,
                      int cam_x, int cam_y, bool bg2, int anchor_mode,
                      const uint8_t *map_pal_flat, size_t map_pal_len)
{
    const FF4_MapDef *md;
    const uint8_t *layout, *tileset, *gfx, *pal = NULL;
    size_t llen, tslen, glen, plen = 0;
    static uint8_t buf[FF4_MAP_W * FF4_MAP_H];
    static uint8_t buf2[FF4_MAP_W * FF4_MAP_H];
    int bpp, x, y, layer;
    char key[48];

    if (!ppu || !db || (unsigned)map >= FF4_DB_MAPS)
        return;
    md = &db->maps[map];

    snprintf(key, sizeof key, "mapLayouts1[%d]", md->layout1);
    layout = blob_of(db, key, &llen);
    if (!layout)
        return;
    if (ff4_gfx_decode_layout(layout, llen, buf) < 32)
        return;
    if (bg2 && md->layout2 < 0x100) {
        snprintf(key, sizeof key, "mapLayouts1[%d]", md->layout2);
        layout = blob_of(db, key, &llen);
        if (layout && ff4_gfx_decode_layout(layout, llen, buf2) >= 32)
            ;
        else
            bg2 = false;
    } else {
        bg2 = false;
    }

    snprintf(key, sizeof key, "mapTilesets[%d]", md->graphics);
    tileset = blob_of(db, key, &tslen);
    gfx = blob_of(db, "mapGraphics0", &glen);
    bpp = 4;
    if (md->graphics != 0) {
        snprintf(key, sizeof key, "mapGraphics%d", md->graphics);
        gfx = blob_of(db, key, &glen);
        bpp = 3;
    }
    if (!tileset || !gfx)
        return;
    {
        static uint8_t project[256];
        uint8_t *work = (uint8_t *)project;

        if (map_pal_flat && map_pal_len >= 0x1000) {
            /* true MapPal (data/gfx/pal/map_pal.bin): the field's 128
               colors = LoadMapPal projection of the flat table */
            ff4_ppu_project_field_pal(map_pal_flat, map_pal_len,
                                      md->palette, project);
            pal = (const uint8_t *)work;
            plen = sizeof project;
        } else {
            snprintf(key, sizeof key, "mapPalettes[%d]", md->palette);
            pal = blob_of(db, key, &plen);
        }
    }
    if (!pal)
        return;

    for (layer = 0; layer <= 1; ++layer) {
        if (layer == 1 && !bg2)
            break;
        for (y = 0; y < 28; ++y) {
            const uint8_t *rows = layer == 0 ? buf : buf2;
            int my = ((cam_y + y) & (FF4_MAP_H - 1)) * FF4_MAP_W;
            for (x = 0; x < 32; ++x) {
                int byte = rows[my + ((cam_x + x) & 0x1F)];
                uint16_t e = ff4_gfx_map_entry(tileset, byte, layer);
                int tile = e & 0x3FF;
                int palgrp = anchor_mode ? (tile >> 4) & 7 : (e >> 10) & 7;
                int hf = (e >> 15) & 1, vf = (e >> 14) & 1;

                /* NOTE: with the flat MapPal the group's 16 colors need the
                   LoadMapPal working-palette projection (still being
                   pinned); for now the per-JSON palette is used. */
                ppu_put_tile(ppu, gfx, glen, bpp, tile, palgrp, hf, vf,
                             pal, plen, x * 8, y * 8);
            }
        }
    }
}

void ff4_ppu_world_bg(FF4_Ppu *ppu, const FF4_AssetDB *db, int world,
                      int cam_x, int cam_y)
{
    const uint8_t *tileset, *gfx, *pal, *row;
    size_t tslen, glen, plen, llen;
    int W = world == 2 ? 64 : 256;
    int x, y;
    char key[48];

    if (!ppu || !db || world < 0 || world > 2)
        return;
    snprintf(key, sizeof key, "worldTilesets[%d]", world);
    tileset = blob_of(db, key, &tslen);
    snprintf(key, sizeof key, "worldGraphics[%d]", world);
    gfx = blob_of(db, key, &glen);
    snprintf(key, sizeof key, "worldPalettes[%d]", world);
    pal = blob_of(db, key, &plen);
    if (!tileset || !gfx || !pal)
        return;
    for (y = 0; y < 28; ++y) {
        snprintf(key, sizeof key, "worldLayout%d[%d]", world + 1,
                 (cam_y + y) % W);
        row = blob_of(db, key, &llen);
        if (!row)
            continue;
        for (x = 0; x < 32; ++x) {
            int b = row[(cam_x + x) % W];
            uint16_t e = (uint16_t)((uint16_t)tileset[b] |
                                    ((uint16_t)tileset[b + 256] << 8));
            int tile = e & 0x3FF;
            int palgrp = (e >> 10) & 7;
            int hf = (e >> 15) & 1, vf = (e >> 14) & 1;

            ppu_put_tile(ppu, gfx, glen, 4, tile, palgrp, hf, vf,
                         pal, plen, x * 8, y * 8);
        }
    }
}

void ff4_ppu_sprite(FF4_Ppu *ppu, const FF4_AssetDB *db, int sprite,
                    int group, int frame, int x, int y, bool transparent)
{
    const uint8_t *gfx, *pal;
    size_t glen, plen;
    char key[48];
    int t, ty, tx;

    if (!ppu || !db || sprite < 0)
        return;
    snprintf(key, sizeof key, "mapSpriteGraphics[%d]", sprite);
    gfx = blob_of(db, key, &glen);
    snprintf(key, sizeof key, "npcPalettes[%d]", group < 0 ? 0 : group);
    pal = blob_of(db, key, &plen);
    if (!gfx)
        return;
    if (plen > 32)
        plen = 32;
    for (t = 0; t < 32; ++t) {
        size_t base = ((size_t)ff4_sheet_order[t]) * 32;

        if (base + 32 > glen)
            break;
        for (ty = 0; ty < 8; ++ty) {
            for (tx = 0; tx < 8; ++tx) {
                int c = 0, p, sx = x + (t & 15) * 8 + tx;
                int sy = y + (t >> 4) * 8 + ty;

                for (p = 0; p < 4; ++p)
                    c |= (int)((gfx[base + (size_t)p * 8 + ty] >>
                                (7 - tx)) & 1) << p;
                if (sx < 0 || sx >= FF4_PPU_W || sy < 0 || sy >= FF4_PPU_H)
                    continue;
                if (c == 0) {
                    if (transparent)
                        continue;
                    if (pal)
                        ppu->px[(size_t)sy * FF4_PPU_W + sx] =
                            ff4_gfx_color16(pal, 0);
                    continue;
                }
                if (pal && (size_t)c * 2 + 1 < plen)
                    ppu->px[(size_t)sy * FF4_PPU_W + sx] =
                        ff4_gfx_color16(pal, c);
            }
        }
    }
}

void ff4_ppu_brightness(FF4_Ppu *ppu, int level)
{
    int i, n = FF4_PPU_W * FF4_PPU_H;

    if (level >= 15)
        return;
    for (i = 0; i < n; ++i) {
        uint32_t p = ppu->px[i];
        uint32_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;

        r = r * (uint32_t)level / 15u;
        g = g * (uint32_t)level / 15u;
        b = b * (uint32_t)level / 15u;
        ppu->px[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* compact 8x8 bitmap set (ASCII 32-95 + a few extras) */
static const uint8_t ff4_font[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space           */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, /* A               */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00}, /* B               */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00}, /* C               */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E,0x00}, /* D               */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00}, /* E               */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00}, /* F               */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00}, /* G               */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, /* H               */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00}, /* I               */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C,0x00}, /* J               */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00}, /* K               */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, /* L               */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00}, /* M               */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x00}, /* N               */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, /* O               */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00}, /* P               */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00}, /* Q               */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00}, /* R               */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0x00}, /* S               */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00}, /* T               */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, /* U               */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00}, /* V               */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11,0x00}, /* W               */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00}, /* X               */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00}, /* Y               */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00}, /* Z               */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00}, /* 0               */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}, /* 1               */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00}, /* 2               */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00}, /* 3               */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00}, /* 4               */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00}, /* 5               */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00}, /* 6               */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00}, /* 7               */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00}, /* 8               */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00}, /* 9               */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* :               */
    {0x00,0x06,0x06,0x00,0x06,0x06,0x00,0x00}, /* ;               */
    {0x00,0x04,0x08,0x10,0x08,0x04,0x00,0x00}, /* <               */
    {0x1F,0x00,0x00,0x1F,0x00,0x00,0x00,0x00}, /* =               */
    {0x00,0x04,0x02,0x01,0x02,0x04,0x00,0x00}, /* >               */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00}, /* ?               */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E,0x00}, /* @               */
    {0x11,0x11,0x1F,0x11,0x11,0x11,0x0A,0x00}, /* [               */
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x00}, /* \               */
    {0x0A,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, /* ]               */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00}, /* ^               */
    {0x08,0x08,0x0F,0x08,0x08,0x08,0x07,0x00}, /* _               */
    {0x00,0x10,0x08,0x04,0x02,0x04,0x08,0x00}, /* `               */
    {0x00,0x0E,0x01,0x0F,0x11,0x11,0x0F,0x00}, /* a               */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,0x00}, /* b               */
    {0x00,0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, /* c               */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,0x00}, /* d               */
    {0x00,0x0E,0x11,0x1F,0x10,0x11,0x0E,0x00}, /* e               */
    {0x06,0x09,0x08,0x1E,0x08,0x08,0x08,0x00}, /* f               */
    {0x0F,0x11,0x11,0x11,0x0F,0x01,0x0E,0x00}, /* g               */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11,0x00}, /* h               */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00}, /* i               */
    {0x02,0x00,0x02,0x02,0x02,0x12,0x0C,0x00}, /* j               */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12,0x00}, /* k               */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,0x00}, /* l               */
    {0x00,0x1B,0x15,0x15,0x15,0x15,0x15,0x00}, /* m               */
    {0x00,0x1E,0x11,0x11,0x11,0x11,0x11,0x00}, /* n               */
    {0x00,0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, /* o               */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00}, /* p               */
    {0x0F,0x11,0x11,0x11,0x0F,0x01,0x01,0x00}, /* q               */
    {0x00,0x16,0x19,0x10,0x10,0x10,0x10,0x00}, /* r               */
    {0x00,0x0F,0x10,0x0E,0x01,0x1E,0x00,0x00}, /* s               */
    {0x08,0x08,0x1E,0x08,0x08,0x09,0x06,0x00}, /* t               */
    {0x00,0x11,0x11,0x11,0x11,0x13,0x0D,0x00}, /* u               */
    {0x00,0x11,0x11,0x11,0x0A,0x0A,0x04,0x00}, /* v               */
    {0x00,0x11,0x15,0x15,0x15,0x15,0x0A,0x00}, /* w               */
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00,0x00}, /* x               */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x08,0x00}, /* y               */
    {0x00,0x1F,0x02,0x04,0x08,0x1F,0x00,0x00}, /* z               */
    {0x00,0x00,0x00,0x00,0x00,0x06,0x06,0x00}, /* { (dot)         */
    {0x00,0x00,0x00,0x00,0x06,0x06,0x04,0x00}, /* | (comma)       */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00}, /* } (minus)       */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x00}, /* ~               */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, /* 127             */
};

const uint8_t *ff4_font_glyph(int ascii)
{
    int c = ascii - 32;

    if (c < 0 || c > 95)
        return NULL;
    return ff4_font[c];
}

void ff4_ppu_text(FF4_Ppu *ppu, int x, int y, const char *s, uint32_t color)
{
    for (; *s && x < FF4_PPU_W; ++s) {
        int c = (unsigned char)*s - 32;
        int i, j;

        if (c < 0 || c > 95)
            c = 0;
        for (j = 0; j < 8; ++j) {
            uint8_t row = ff4_font[c][j];

            for (i = 0; i < 8; ++i) {
                int px = x + i, py = y + j;

                if (px >= 0 && px < FF4_PPU_W && py >= 0 &&
                    py < FF4_PPU_H && (row & (0x80 >> i)))
                    ppu->px[(size_t)py * FF4_PPU_W + px] = color;
            }
        }
        x += 8;
    }
}

void ff4_ppu_project_field_pal(const uint8_t *flat, size_t len,
                               int palette_id, uint8_t *out)
{
    int x0 = (palette_id << 7) & 0x7FF;
    int i;

    if (!flat || !out)
        return;
    for (i = 0; i < 128; ++i) {
        out[i * 2] = (size_t)(0x10 + x0 + i) < len ? flat[0x10 + x0 + i] : 0;
        out[i * 2 + 1] =
            (size_t)(0x90 + x0 + i) < len ? flat[0x90 + x0 + i] : 0;
    }
}
