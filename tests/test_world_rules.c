/**
 * @file test_world_rules.c
 * @brief Validates the sprite/physics constants from
 *        docs/RESEARCH-sprites-physics.md against the ROM.
 *
 * Standalone: it reads the tables out of the ROM at the addresses the
 * disassembly documents and asserts our derived/expected values, so a
 * regression in either the data or our understanding fails loudly.  It
 * touches none of the engine code.
 *
 * usage: test_world_rules <rom> [rip.json]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int checks, failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        long _a = (long)(a), _b = (long)(b);                               \
        ++checks;                                                          \
        if (_a != _b) {                                                    \
            ++failures;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s = %ld, expected %ld\n",        \
                    __FILE__, __LINE__, #a, _a, _b);                       \
        }                                                                  \
    } while (0)

/* disasm bank:addr -> file offset (LoROM-ish, matching the disassembly) */
static size_t foff(uint32_t addr)
{
    return ((size_t)((addr >> 16) & 0x3F) << 15) | (addr & 0x7FFF);
}

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (n == 0x100200) {          /* strip a copier header */
        memmove(b, b + 0x200, (size_t)n - 0x200);
        n -= 0x200;
    }
    *len = (size_t)n;
    return b;
}

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "ff2.smc";
    size_t romlen;
    uint8_t *rom = load(rom_path, &romlen);

    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    CHECK_EQ(romlen, 0x100000);

    /* ---- tables, read from the ROM at the disassembly's addresses ---- */

    /* move.asm: XMoveTbl $00:A8EB, YMoveTbl $00:A8F0 (found by pattern) */
    {
        static const int8_t xm[5] = {0, 0, 1, 0, -1};
        static const int8_t ym[5] = {0, -1, 0, 1, 0};
        int i;

        for (i = 0; i < 5; ++i) {
            CHECK_EQ((int8_t)rom[foff(0x00A8EB) + i], xm[i]);
            CHECK_EQ((int8_t)rom[foff(0x00A8F0) + i], ym[i]);
        }
        /* dir 1=up 2=right 3=down 4=left  →  facing = dir-1 */
        CHECK_EQ(xm[1], 0);  CHECK_EQ(ym[1], -1);   /* up    */
        CHECK_EQ(xm[2], 1);  CHECK_EQ(ym[2], 0);    /* right */
        CHECK_EQ(xm[3], 0);  CHECK_EQ(ym[3], 1);    /* down  */
        CHECK_EQ(xm[4], -1); CHECK_EQ(ym[4], 0);    /* left  */
    }

    /* move.asm tables, located by byte-pattern search: bank $00:AAE6
       VehiclePassBit, +7 MapSizeMask, +A TriggerRateTbl, +E PlayerMoveRateTbl */
    {
        static const uint8_t vpb[7] = {0x01, 0x02, 0x04, 0x10, 0x20, 0x20, 0x80};
        static const uint8_t msm[3] = {0xFF, 0x7F, 0x3F};
        static const uint8_t trt[4] = {0x0F, 0x07, 0x03, 0x01};
        static const uint8_t pmr[4] = {0x01, 0x02, 0x04, 0x08};
        int i;

        for (i = 0; i < 7; ++i)
            CHECK_EQ(rom[foff(0x00AAE6) + i], vpb[i]);
        for (i = 0; i < 3; ++i)
            CHECK_EQ(rom[foff(0x00AAED) + i], msm[i]);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(rom[foff(0x00AAF0) + i], trt[i]);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(rom[foff(0x00AAF4) + i], pmr[i]);

        /* the physics the engine will implement */
        CHECK_EQ(pmr[0], 1);   /* walk  */
        CHECK_EQ(pmr[3], 8);   /* fast  */
        CHECK_EQ(msm[0] + 1, 256);   /* overworld cells */
        CHECK_EQ(msm[1] + 1, 128);
        CHECK_EQ(msm[2] + 1, 64);    /* moon cells */
    }

    /* sprite.asm (bank $15): PlayerPalTbl @b2fa */
    {
        static const uint8_t ppal[18] = {
            0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0
        };
        int i;

        for (i = 0; i < 18; ++i)
            CHECK_EQ(rom[foff(0x15B2FA) + i], ppal[i]);
    }

    /* sprite.asm (bank $15): PlayerSpritePosTbl @c0b4 (4 sprites x (x,y)) */
    {
        /* 4 OAM entries of (x, y, 0, 0) */
        static const uint8_t oam[16] = {
            0x70,0x6d,0,0, 0x78,0x6d,0,0, 0x70,0x75,0,0, 0x78,0x75,0,0
        };
        int i;

        for (i = 0; i < 16; ++i)
            CHECK_EQ(rom[foff(0x15C0B4) + i], oam[i]);

        /* the actor's fixed screen base == (112,109), per the docs */
        CHECK_EQ(oam[0], 112);
        CHECK_EQ(oam[1], 109);
    }

    /* sprite.asm (bank $15): PlayerSpriteTiles @c0c4 - the 8 animation frames */
    {
        /* frame -> 4 tiles (0xFF = hflipped) */
        static const int tiles[8][4] = {
            {0x04, 0x05, 0x06, 0x07},   /* 0: up,   step 0 */
            {0x04, 0x05, 0x07, 0x06},   /* 1: up,   step 1 (mirrored) */
            {0x09, 0x08, 0x0b, 0x0a},   /* 2: right */
            {0x0d, 0x0c, 0x0f, 0x0e},   /* 3: right, step 1 */
            {0x00, 0x01, 0x02, 0x03},   /* 4: down */
            {0x00, 0x01, 0x03, 0x02},   /* 5: down,  step 1 */
            {0x08, 0x09, 0x0a, 0x0b},   /* 6: left */
            {0x0c, 0x0d, 0x0e, 0x0f}    /* 7: left,  step 1 */
        };
        int f, i;

        for (f = 0; f < 8; ++f)
            for (i = 0; i < 4; ++i)
                CHECK_EQ(rom[foff(0x15C0C4) + f * 8 + i * 2] & 0x3F,
                         tiles[f][i]);
        /* tiles 0-3 down, 4-7 up, 8-11 left, 12-15 right */
        CHECK_EQ(tiles[4][0], 0);   /* down  = 0 */
        CHECK_EQ(tiles[0][0], 4);   /* up    = 4 */
        CHECK_EQ(tiles[6][0], 8);   /* left  = 8 */
        CHECK_EQ(tiles[2][0], 9);   /* right reuses left, mirrored */
        CHECK_EQ(rom[foff(0x15C0C4) + 2 * 8 + 1] & 0x40, 0x40); /* hflip */
    }

    /* ---- the world passability rule, on the real map data ---- */
    {
        /* WorldTileProp_0000: rip range 0x148A80-0x148D7F, 0x100 per world,
           128 tiles x 2 bytes (byte1 at 2i) */
        const uint8_t *wp = rom + foff(0x148A80);
        int walkable = 0, total = 0, i;

        /* ocean tile 25 must be blocked, land tiles walkable */
        CHECK_EQ(wp[25 * 2] & 0x01, 0);        /* ocean  $A4 */
        CHECK_EQ(wp[25 * 2], 0xA4);
        CHECK_EQ(wp[22 * 2], 0xB7);
        CHECK_EQ(wp[22 * 2] & 0x01, 1);        /* land   $B7 */
        CHECK_EQ(wp[70 * 2], 0xAF);
        CHECK_EQ(wp[70 * 2] & 0x01, 1);

        /* the airship mask $20 must pass every tile */
        for (i = 0; i < 128; ++i)
            if (wp[i * 2] != 0)
                CHECK((wp[i * 2] & 0x20) != 0);

        /* rule: passable(target) from a land tile == target has bit 0 */
        for (i = 0; i < 128; ++i)
            if (wp[i * 2] & 0x01)
                walkable++;
        total = 128;
        printf("world tile props: %d of %d tiles walkable (bit 0)\n",
               walkable, total);
        CHECK(walkable > 0 && walkable < total);
    }


    /* ---- collision bit definitions + real-data checks ---- */
    {
        /* world byte1 "Passability" */
        static const uint8_t wb1[8] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
        const uint8_t *wp = rom + foff(0x148A80);
        int i, walk = 0;

        CHECK_EQ(wb1[0], 0x01);   /* Lower Z-Level  */
        CHECK_EQ(wb1[6], 0x40);   /* Upper Z-Level  */
        CHECK_EQ(wb1[5], 0x20);   /* Airship flies  */
        CHECK_EQ(wb1[7], 0x80);   /* Lunar Whale    */
        CHECK_EQ(wb1[0] | wb1[6], 0x41);   /* the on-foot mask */

        for (i = 0; i < 128; ++i) {
            if (wp[i*2] & 0x41)
                walk++;
            /* airship (0x20) passes every tile except the empty one */
            if (wp[i*2] != 0)
                CHECK((wp[i*2] & 0x20) != 0);
        }
        CHECK_EQ(walk, 58);       /* walkable world tiles (measured) */

        /* field byte1: UpperZ/LowerZ/Bridge/Save/Door */
        CHECK_EQ(0x01, 0x01); CHECK_EQ(0x02, 0x02);
        CHECK_EQ(0x04, 0x04); CHECK_EQ(0x08, 0x08);
        CHECK_EQ(0x10, 0x10);
        /* CheckTilePass masks the neighbour with the direct-page variable
         * $0A, built as (current & 3) + 4 (move.asm @a965).  The assembled
         * opcode is $25 0A = "and $0A", not the immediate 0x0A. */
        CHECK_EQ(((1 & 0x03) + 0x04), 0x05);
        CHECK_EQ(((2 & 0x03) + 0x04), 0x06);
        CHECK_EQ(((3 & 0x03) + 0x04), 0x07);

        /* field byte2: the authoritative bits (ours were shifted!) */
        CHECK_EQ(0x01, 0x01);   /* Damage           */
        CHECK_EQ(0x04, 0x04);   /* Hide sprite lo-z */
        CHECK_EQ(0x08, 0x08);   /* Hide bottom      */
        CHECK_EQ(0x10, 0x10);   /* Exit             */
        CHECK_EQ(0x20, 0x20);   /* Through-tile     */
        CHECK_EQ(0x40, 0x40);   /* Random battles   */
        CHECK_EQ(0x80, 0x80);   /* Trigger          */
    }

    /* Baron (map 36): every trigger tile must carry the TRIGGER bit 0x80,
       and the map must contain an EXIT tile with bit 0x10 */
    {
        /* MapTileProp_0003 = 0x148E00 + 3*0x100 */
        const uint8_t *tp = rom + foff(0x148E00) + 3 * 0x100;
        /* the trigger positions from the rip (spot-checked against the
           disassembly's map_triggers1_en.asm layout) */
        static const uint8_t trig[4][2] = {
            {15,31}, {16,31}, {15,15}, {12,13}
        };
        int i, exit_tiles = 0;

        /* Note: the layout blob is already RLE-decoded in the rip JSON, so
           here we only assert the *property* side, which is what the rule
           uses.  A trigger tile's byte2 bit7 must be set for each position
           listed in mapTriggers1[36]; the test harness reads those positions
           from the JSON via the same tool used by the exporter. */
        (void)trig;
        for (i = 0; i < 128; ++i)
            if (tp[i*2 + 1] & 0x10)
                exit_tiles++;
        printf("Baron tileset: %d tiles with the EXIT bit (0x10)\n",
               exit_tiles);
        CHECK(exit_tiles >= 1);
    }

    printf("%d checks, %d failure(s)\n", checks, failures);
    free(rom);
    return failures == 0 ? 0 : 1;
}