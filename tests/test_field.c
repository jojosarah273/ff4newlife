/**
 * @file test_field.c
 * @brief Phase 6 tests: collision rules (CheckTilePass port), movement,
 *        tile effects (exit/trigger), random encounters, camera.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/engine/field_engine.h"
#include "ff4/core/ff4_state.h"

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

/* ------------------------------------------------------------------ */
/*  Pure collision rule (move.asm @aa58)                               */
/* ------------------------------------------------------------------ */

static void test_tile_pass_rule(void)
{
    /* CheckTilePass (move.asm @aa58) with the REAL mask.
     *
     * The assembled opcode at @aa7d is $25 0A = "and $0A" (direct page), and
     * move.asm @a965 builds that variable as (current & 3) + 4.  It is NOT the
     * immediate 0x0A - reading it that way makes most floors solid.
     *
     * bits 0/1 of byte1 are the passable lanes, bit2 is "bridge".
     */
    /* lane 1 stander (z=1) -> mask 5: bit0 or bit2 passes */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x01, 1) == 1);   /* lane 1 tile  */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x05, 1) == 1);   /* lane 1+brg   */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x04, 1) == 1);   /* bridge       */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x02, 1) == 0);   /* other lane   */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x00, 1) == 0);   /* void         */
    /* lane 2 stander (z=2) -> mask 6: bit1 or bit2 passes */
    CHECK(ff4_field_tile_pass_bytes(0x02, 0x02, 2) == 1);
    CHECK(ff4_field_tile_pass_bytes(0x02, 0x06, 2) == 1);
    CHECK(ff4_field_tile_pass_bytes(0x02, 0x01, 2) == 0);
    /* lane 3 stander -> mask 7: any lane or a bridge */
    CHECK(ff4_field_tile_pass_bytes(0x03, 0x01, 3) == 1);
    CHECK(ff4_field_tile_pass_bytes(0x03, 0x02, 3) == 1);
    CHECK(ff4_field_tile_pass_bytes(0x03, 0x00, 3) == 0);
    /* lane 0 stander -> mask 4: only a bridge */
    CHECK(ff4_field_tile_pass_bytes(0x00, 0x04, 0) == 1);
    CHECK(ff4_field_tile_pass_bytes(0x00, 0x01, 0) == 0);
    CHECK(ff4_field_tile_pass_bytes(0x00, 0x02, 0) == 0);

    /* standing on a bridge (bit 2): the standing lane is kept ($d2 does not
     * update on bridge tiles), so match the lane or step onto another bridge */
    CHECK(ff4_field_tile_pass_bytes(0x04, 0x02, 0x02) == 1);  /* lane match   */
    CHECK(ff4_field_tile_pass_bytes(0x04, 0x04, 0x02) == 1);  /* brg->brg     */
    CHECK(ff4_field_tile_pass_bytes(0x04, 0x00, 0x02) == 0);  /* else blocked */
    CHECK(ff4_field_tile_pass_bytes(0x04, 0x01, 0x02) == 0);
    /* the mask never uses bit3+ */
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x08, 1) == 0);
    CHECK(ff4_field_tile_pass_bytes(0x01, 0x80, 1) == 0);
}

/* ------------------------------------------------------------------ */
/*  Map resolution + tile props                                        */
/* ------------------------------------------------------------------ */

static void test_map_tile_props(const FF4_AssetDB *db)
{
    FF4_GameState g;
    uint8_t b1, b2;
    int i, found_exit = -1, ex = 0, ey = 0;
    uint8_t tset;

    ff4_state_init(&g);
    g.world.map_id = 0;                     /* Town of Baron */
    g.world.map_type = FF4_MAP_SUB;
    CHECK(ff4_field_load_map(db, &g) == 0);
    CHECK(g.world.map_type == FF4_MAP_SUB);

    tset = db->maps[0].graphics;
    CHECK(tset < FF4_DB_TILESETS);

    /* tile props resolve without crashing for any position */
    ff4_field_tile_props(db, &g, 0, 0, &b1, &b2);
    ff4_field_tile_props(db, &g, 255, 127, &b1, &b2);

    /* find an exit tile to test tile effects (data-driven) */
    for (i = 0; i < 256 && found_exit < 0; ++i) {
        if (db->map_tile_props[tset][i][1] & FF4_TP2_EXIT) {
            found_exit = i;
            ex = i & 0x1F;
            ey = i >> 4;
            break;
        }
    }
    CHECK(found_exit >= 0);
    if (found_exit >= 0) {
        g.world.x = (uint16_t)ex;
        g.world.y = (uint16_t)ey;
        ff4_field_tile_props(db, &g, ex, ey, &b1, &b2);
        CHECK(b2 & FF4_TP2_EXIT);
    }
}

/* ------------------------------------------------------------------ */
/*  Movement                                                           */
/* ------------------------------------------------------------------ */

static void test_movement(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_FieldStep step;
    uint8_t b1, b2;
    int x, y;

    ff4_state_init(&g);
    g.world.map_id = 0;
    g.world.map_type = FF4_MAP_SUB;
    ff4_field_load_map(db, &g);

    /* find a spot with an open neighbour and a blocked one */
    for (y = 2; y < 120; ++y) {
        for (x = 2; x < 30; ++x) {
            int pass_r = 0, pass_l = 0;
            uint8_t c1, c2, z;

            g.world.x = (uint16_t)x;
            g.world.y = (uint16_t)y;
            ff4_field_tile_props(db, &g, x, y, &c1, &c2);
            z = (uint8_t)(c1 & FF4_TP1_Z_MASK);
            ff4_field_tile_props(db, &g, x + 1, y, &b1, &b2);
            pass_r = ff4_field_tile_pass_bytes(c1, b1, z);
            ff4_field_tile_props(db, &g, x - 1, y, &b1, &b2);
            pass_l = ff4_field_tile_pass_bytes(c1, b1, z);

            if (pass_r && pass_l) {
                /* move right then left */
                step = ff4_field_frame(db, &g, FF4_DIR_RIGHT);
                CHECK(step.moved);
                CHECK(g.world.x == x + 1);
                step = ff4_field_frame(db, &g, FF4_DIR_LEFT);
                CHECK(step.moved);
                CHECK(g.world.x == x);
                return;
            }
        }
    }
    CHECK(0);   /* could not find an open pair on this tileset */
}

static void test_movement_blocked(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_FieldStep step;
    uint8_t b1, b2;
    int x, y;

    ff4_state_init(&g);
    g.world.map_id = 0;
    ff4_field_load_map(db, &g);

    /* event running blocks all movement (move.asm @aa5f) */
    g.world.event_running = true;
    step = ff4_field_frame(db, &g, FF4_DIR_DOWN);
    CHECK(!step.moved);
    g.world.event_running = false;

    /* player control off blocks movement */
    g.world.player_control = false;
    step = ff4_field_frame(db, &g, FF4_DIR_DOWN);
    CHECK(!step.moved);
    g.world.player_control = true;

    /* blocked tiles keep the party in place */
    for (y = 2; y < 120; ++y)
        for (x = 2; x < 30; ++x) {
            uint8_t nb1;

            ff4_field_tile_props(db, &g, x, y, &b1, &b2);
            {
                uint8_t z = (uint8_t)(b1 & FF4_TP1_Z_MASK);

                g.world.x = (uint16_t)x;
                g.world.y = (uint16_t)y;
                ff4_field_tile_props(db, &g, x + 1, y, &nb1, &b2);
                if (!ff4_field_tile_pass_bytes(b1, nb1, z)) {
                    uint16_t ox = g.world.x;

                    step = ff4_field_frame(db, &g, FF4_DIR_RIGHT);
                    CHECK(!step.moved);
                    CHECK(g.world.x == ox);
                    return;
                }
            }
        }
    /* acceptable: this tileset may have no blocked tiles */
}

/* ------------------------------------------------------------------ */
/*  Random encounters                                                  */
/* ------------------------------------------------------------------ */

static void test_random_battle(const FF4_AssetDB *db)
{
    FF4_GameState g;
    uint8_t map = 1;

    ff4_state_init(&g);
    g.world.map_id = map;
    g.world.map_type = FF4_MAP_SUB;
    g.world.battle_enabled = true;

    if (db->map_battle[map] == 0 || db->map_battle_rates[map] == 0) {
        printf("  note: map %d has no random battles, skipping\n", map);
        return;
    }
    CHECK(ff4_field_random_battle(db, &g) >= -1);

    /* disabled battles never roll */
    g.world.battle_enabled = false;
    CHECK(ff4_field_random_battle(db, &g) == -1);
    g.world.battle_enabled = true;

    /* a rolled formation must reference an existing formation */
    for (int i = 0; i < 64; ++i) {
        int f = ff4_field_random_battle(db, &g);

        if (f >= 0)
            CHECK(f < FF4_DB_FORMATIONS);
        g.world.frame_counter++;
    }
}

/* ------------------------------------------------------------------ */
/*  Camera                                                             */
/* ------------------------------------------------------------------ */

static void test_camera(void)
{
    FF4_GameState g;
    FF4_Camera c;

    ff4_state_init(&g);
    g.world.x = 30;
    g.world.y = 20;
    c = ff4_field_camera(&g);
    CHECK(c.tile_x == 30);
    CHECK(c.tile_y == 20);
    CHECK(c.sub_x == 0 && c.sub_y == 0);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_tile_pass_rule();
    test_map_tile_props(&db);
    test_movement(&db);
    test_movement_blocked(&db);
    test_random_battle(&db);
    test_camera();

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}