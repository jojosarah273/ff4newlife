/* test_dialog: lock the message box down.
 *
 * Verifies the window tile bank and the layout constants against the values
 * measured from a real snes9x frame (see include/ff4/core/ff4_dialog_layout.h).
 * If this test fails, the message box has regressed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff4/core/ff4_dialog_layout.h"
#include "ff4/core/ff4_window_text.h"

static int checks, failures;

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

static unsigned char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *len = (size_t)n;
    return buf;
}

/* one row of a 2bpp tile -> 8 palette indices */
static void tile_row(const unsigned char *gfx, int tile, int y, int out[8])
{
    unsigned p0 = gfx[tile * 16 + y * 2];
    unsigned p1 = gfx[tile * 16 + y * 2 + 1];
    int x;

    for (x = 0; x < 8; ++x) {
        int b = 7 - x;

        out[x] = (int)(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1));
    }
}

static int row_is(const unsigned char *gfx, int tile, int y, int v)
{
    int row[8], x;

    tile_row(gfx, tile, y, row);
    for (x = 0; x < 8; ++x)
        if (row[x] != v)
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "data/gfx/window";
    char path[512];
    size_t glen = 0, plen = 0;
    unsigned char *gfx, *pal;

    /* ---- layout constants: the emulator-measured numbers ---------------- */
    CHECK(FF4_DLG_X == 8);
    CHECK(FF4_DLG_W == 240);
    CHECK(FF4_DLG_TOP_Y == 12);
    CHECK(FF4_DLG_BODY_ROWS == 8);      /* $df goes 1..8 */
    CHECK(FF4_DLG_ROWS == 10);          /* + one border row top and bottom */
    CHECK(FF4_DLG_TEXT_X == FF4_DLG_X + 16);
    CHECK(FF4_DLG_TEXT_Y0 == FF4_DLG_TOP_Y + 15);
    CHECK(FF4_DLG_TEXT_PITCH == 16);
    CHECK(FF4_DLG_COLS == 26);
    CHECK(FF4_DLG_LINES == 4);
    /* last text line must still be inside the box */
    CHECK(FF4_DLG_TEXT_Y0 + (FF4_DLG_LINES - 1) * FF4_DLG_TEXT_PITCH + 8
          <= FF4_DLG_TOP_Y + FF4_DLG_ROWS * 8);

    /* ---- text codes (ripemd charTable) --------------------------------- */
    CHECK(ff4_ascii_to_code['A' - 0x20] == 0x42);
    CHECK(ff4_ascii_to_code['Z' - 0x20] == 0x5B);
    CHECK(ff4_ascii_to_code['a' - 0x20] == 0x5C);
    CHECK(ff4_ascii_to_code['z' - 0x20] == 0x75);
    CHECK(ff4_ascii_to_code['0' - 0x20] == 0x80);
    CHECK(ff4_ascii_to_code[' ' - 0x20] == 0xFF);
    CHECK(sizeof ff4_ascii_to_code == 96);

    /* ---- window tile bank ---------------------------------------------- */
    snprintf(path, sizeof path, "%s/window_gfx.bin", dir);
    gfx = read_all(path, &glen);
    CHECK(gfx != NULL);
    if (!gfx) {
        printf("test_dialog: %d checks, %d failures (missing assets)\n",
               checks, failures);
        return 1;
    }
    CHECK(glen == 4096);                       /* 256 tiles x 2bpp */

    /* the top border tiles only draw their LOWER half (rows 4..7).  Rows 0..3
     * must be colour index 0 == transparent, otherwise a black bar appears
     * above the box (this exact bug shipped once). */
    CHECK(row_is(gfx, FF4_DLG_TILE_TL, 0, 0));
    CHECK(row_is(gfx, FF4_DLG_TILE_TL, 3, 0));
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 0, 0));
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 3, 0));
    CHECK(row_is(gfx, FF4_DLG_TILE_TR, 0, 0));
    CHECK(row_is(gfx, FF4_DLG_TILE_TR, 3, 0));
    /* ... and rows 4..7 are the grey/white/grey/navy ring */
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 4, 2));
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 5, 3));
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 6, 2));
    CHECK(row_is(gfx, FF4_DLG_TILE_TOP, 7, 1));
    /* bottom border: navy body then grey/white/grey */
    CHECK(row_is(gfx, FF4_DLG_TILE_BOT, 4, 1));
    CHECK(row_is(gfx, FF4_DLG_TILE_BOT, 5, 2));
    CHECK(row_is(gfx, FF4_DLG_TILE_BOT, 6, 3));
    CHECK(row_is(gfx, FF4_DLG_TILE_BOT, 7, 2));
    /* body fill tile is solid navy (colour 1) */
    CHECK(row_is(gfx, FF4_DLG_TILE_BODY, 0, 1));
    CHECK(row_is(gfx, FF4_DLG_TILE_BODY, 7, 1));
    /* side borders: grey/white/grey then navy */
    {
        int row[8];

        tile_row(gfx, FF4_DLG_TILE_L, 0, row);
        CHECK(row[0] == 2 && row[1] == 3 && row[2] == 2);
        CHECK(row[3] == 1);
        tile_row(gfx, FF4_DLG_TILE_R, 0, row);
        CHECK(row[5] == 2 && row[6] == 3 && row[7] == 2);
        CHECK(row[4] == 1);
    }
    /* 'A' (tile 0x42) must have ink, and the font must not be blank */
    {
        int row[8], x, ink = 0;

        for (x = 0; x < 8; ++x) {
            tile_row(gfx, 0x42, x, row);
            {
                int k;

                for (k = 0; k < 8; ++k)
                    if (row[k])
                        ++ink;
            }
        }
        CHECK(ink > 8);
    }
    free(gfx);

    /* ---- palette -------------------------------------------------------- */
    snprintf(path, sizeof path, "%s/window_pal.bin", dir);
    pal = read_all(path, &plen);
    CHECK(pal != NULL);
    if (pal) {
        CHECK(plen == 32);                     /* 16 colours BGR555 */
        /* colour 1 (the box body) must be a dark blue, not black/white */
        {
            unsigned w = (unsigned)pal[2] | ((unsigned)pal[3] << 8);

            CHECK((w & 0x1F) < 8);             /* low red */
            CHECK(((w >> 5) & 0x1F) < 8);      /* low green */
            CHECK(((w >> 10) & 0x1F) > 8);     /* some blue */
        }
        free(pal);
    }

    printf("test_dialog: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
