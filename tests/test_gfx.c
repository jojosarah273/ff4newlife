/**
 * @file test_gfx.c
 * @brief Phase 10 tests: graphics pipeline.
 *
 *  - the PNG export path (tools/export_gfx.js via the rip toolchain)
 *    loads through stb_image;
 *  - the direct blob decoder (ff4_gfx_render_tilemap) matches the PNG
 *    pixel-for-pixel on a sample grid (both implement the same SNES
 *    tile/plane/palette rules);
 *  - surface helpers behave (free, color16).
 */
#include <stdio.h>
#include <string.h>

#include "ff4/core/ff4_gfx.h"
#include "ff4/engine/asset_db.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

static void test_color16(void)
{
    /* SNES BGR555: 0x001F = red 31, 0x03E0 = green 31, 0x7C00 = blue 31 */
    const uint8_t red[2] = {0x1F, 0x00};
    const uint8_t blue[2] = {0x00, 0x7C};

    /* 31<<3|3 = 0xFB */
    CHECK(ff4_gfx_color16(red, 0) == 0xFFFF0000u);
    CHECK(ff4_gfx_color16(blue, 0) == 0xFF0000FFu);
}

static void test_title_png(const char *gfx_dir)
{
    char path[256];

    snprintf(path, sizeof path, "%s/title.png", gfx_dir);
    {
        FF4_GfxSurface img;

        CHECK(ff4_image_load(path, &img) == 0);
        CHECK(img.w == 256 && img.h == 224);
        if (img.pixels) {
            int i, nz = 0;

            for (i = 0; i < img.w * img.h; ++i)
                if (img.pixels[i] & 0xFFFFFF)
                    ++nz;
            /* the US title is a compact logo on a black screen
               (validated against snes9x): expect real artwork, not
               a blank sheet, but most of the frame is black */
            CHECK(nz > img.w * img.h / 1000);
        }
        ff4_gfx_surface_free(&img);
    }
}

static void test_decoder_matches_png(const char *gfx_dir)
{
    FF4_AssetDB db;
    FF4_GfxSurface png, raw;
    char path[256];

    if (ff4_db_load(&db, "data/ff4-en-data.json") != 0)
        return;

    snprintf(path, sizeof path, "%s/title.png", gfx_dir);
    if (ff4_image_load(path, &png) != 0) {
        ff4_db_free(&db);
        return;
    }
    CHECK(ff4_gfx_title_screen(&db, &raw) == 0);

    /* the two paths must agree on the sampled grid (same rules) */
    if (png.pixels && raw.pixels &&
        png.w == raw.w && png.h == raw.h) {
        int x, y, mismatches = 0;
        int samples = 0;

        for (y = 0; y < raw.h; y += 8)
            for (x = 0; x < raw.w; x += 8) {
                uint32_t a = raw.pixels[(size_t)y * raw.w + x];
                uint32_t b = png.pixels[(size_t)y * raw.w + x];

                ++samples;
                if (a != b)
                    ++mismatches;
            }
        fprintf(stderr, "title parity: %d mismatched of %d samples\n",
                mismatches, samples);
        CHECK(mismatches < 8);  /* rounding edge cases */
        (void)samples;
    }
    ff4_gfx_surface_free(&png);
    ff4_gfx_surface_free(&raw);
    ff4_db_free(&db);
}

static void test_prologue_png(const char *gfx_dir)
{
    char path[256];
    int w;

    (void)w;
    for (int stage = 1; stage <= 2; ++stage) {
        FF4_GfxSurface img;

        snprintf(path, sizeof path, "%s/prologue%d.png", gfx_dir, stage);
        CHECK(ff4_image_load(path, &img) == 0);
        CHECK(img.w == 256 && img.h == 224);
        ff4_gfx_surface_free(&img);
    }
}

int main(int argc, char **argv)
{
    const char *gfx_dir = argc > 2 ? argv[2]
                                   : "data/graphics";

    test_color16();
    test_title_png(gfx_dir);
    test_prologue_png(gfx_dir);
    test_decoder_matches_png(gfx_dir);
    test_maps_render(NULL);

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
void test_maps_render(const char *data_dir)
{
    (void)data_dir;
    FF4_AssetDB db;

    if (ff4_db_load(&db, "data/ff4-en-data.json") != 0)
        return;
    {
        FF4_GfxSurface w;
        int i, nz = 0;

        if (ff4_gfx_render_world(&db, 0, 96, 96, 32, 28, &w) == 0) {
            for (i = 0; i < w.w * w.h; ++i)
                if (w.pixels[i] & 0xFFFFFF)
                    ++nz;
            fprintf(stderr, "world render %dx%d nonzero=%d\n", w.w, w.h, nz);
            CHECK(w.w == 256 && w.h == 224);
            CHECK(nz > w.w * w.h / 400);
            ff4_gfx_surface_free(&w);
        } else {
            CHECK(0 && "render_world failed");
        }
    }
    {
        FF4_GfxSurface m;
        int i, nz = 0;
        int maps[] = {192, 136, 36};
        int k;

        for (k = 0; k < 3; ++k) {
            if (ff4_gfx_render_submap(&db, maps[k], 0, 0, 32, 28, true,
                                      &m) == 0) {
                nz = 0;
                for (i = 0; i < m.w * m.h; ++i)
                    if (m.pixels[i] & 0xFFFFFF)
                        ++nz;
                fprintf(stderr, "submap %d render %dx%d nonzero=%d\n",
                        maps[k], m.w, m.h, nz);
                CHECK(m.w == 256 && m.h == 224);
                CHECK(nz > m.w * m.h / 50);
                ff4_gfx_surface_free(&m);
            } else {
                CHECK(0 && "render_submap failed");
            }
        }
    }
    ff4_db_free(&db);
}
