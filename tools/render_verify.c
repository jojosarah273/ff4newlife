/* render_verify: headless PPU render -> PNG for emulator diffing.
 * usage: render_verify <data.json> <map> <camx> <camy> <anchor> <bg2> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/core/ff4_gfx.h"
#include "ff4/resource/ff4_bundle.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    FF4_Ppu ppu;
    int map, camx, camy, anchor, bg2;

    if (argc < 8) {
        fprintf(stderr, "usage: %s data.json map camx camy anchor bg2 out.png\n",
                argv[0]);
        return 1;
    }
    if (ff4_db_load(&db, argv[1]) != 0) {
        fprintf(stderr, "db load failed\n");
        return 1;
    }
    map = atoi(argv[2]);
    camx = atoi(argv[3]);
    camy = atoi(argv[4]);
    anchor = atoi(argv[5]);
    bg2 = atoi(argv[6]);

    {
        FF4_Bundle b;
        size_t plen = 0;
        const uint8_t *flat = NULL;

        if (ff4_bundle_load(&b, "data/gfx") == 0)
            flat = ff4_bundle_bytes(&b, "pal/map_pal.bin", &plen);
        memset(&ppu, 0, sizeof ppu);
        ff4_ppu_field_bg(&ppu, &db, map, camx, camy, bg2 != 0, anchor,
                         flat, (size_t)(plen >= 0x1000 ? plen : 0));
        ff4_bundle_free(&b);
    }
    if (ff4_png_write(argv[7], FF4_PPU_W, FF4_PPU_H, ppu.px) != 0) {
        fprintf(stderr, "png write failed\n");
        return 1;
    }
    {
        int i, nz = 0;
        for (i = 0; i < FF4_PPU_W * FF4_PPU_H; ++i)
            if (ppu.px[i] & 0xFFFFFF)
                ++nz;
        fprintf(stderr, "rendered %s: nz=%d\n", argv[7], nz);
    }
    ff4_db_free(&db);
    return 0;
}