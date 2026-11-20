/**
 * @file test_scene.c
 * @brief Phase 9 tests: scene machine transitions (field -> battle ->
 *        field; menu overlay) with the real engines.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/engine/battle_engine.h"
#include "ff4/engine/scene_machine.h"
#include "ff4/core/ff4_state.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
        }                                                                      \
    } while (0)

static void strong_party(FF4_GameState *g)
{
    int i;

    ff4_party_add(g, FF4_CHAR_CECIL_DK, 0);
    ff4_party_add(g, FF4_CHAR_KAIN_1, 1);
    ff4_party_add(g, FF4_CHAR_RYDIA_CHILD, 2);
    for (i = 0; i < 3; ++i) {
        g->party[i].max_hp = 300;
        g->party[i].hp = 300;
        g->party[i].max_mp = 30;
        g->party[i].mp = 30;
        g->party[i].attack_power = 40;
        g->party[i].attack_mult = 2;
        g->party[i].hit_rate = 90;
        g->party[i].crit_rate = 2;
        g->party[i].crit_bonus = 20;
        g->party[i].equip[FF4_EQ_RIGHT_HAND] = 0x16;
    }
}

static void test_field_to_battle(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_SceneCtx sc;
    int ticks = 0;
    bool done = false;

    ff4_state_init(&g);
    strong_party(&g);
    g.world.map_id = 0;
    g.world.x = 10;
    g.world.y = 20;

    ff4_scene_init(&sc);
    CHECK(sc.scene == FF4_SCENE_OPENING);      /* boots at the title */
    ff4_scene_advance_opening(&sc);            /* title -> prologue 1 */
    ff4_scene_advance_opening(&sc);            /* prologue 1 -> prologue 2 */
    ff4_scene_advance_opening(&sc);            /* prologue 2 -> field */
    CHECK(sc.scene == FF4_SCENE_FIELD);

    ff4_scene_request_battle(&sc, 0);          /* 3x Imp */
    ff4_scene_frame(db, &g, &sc, FF4_DIR_NONE);
    CHECK(sc.scene == FF4_SCENE_BATTLE);
    CHECK(g.battle.active);

    /* run the battle to its conclusion (auto-fight) */
    while (!done && ticks++ < 1000)
        done = !ff4_scene_frame(db, &g, &sc, FF4_DIR_NONE) ||
               sc.scene == FF4_SCENE_FIELD;

    CHECK(sc.scene == FF4_SCENE_FIELD);
    /* victory rewards landed in the field state + battle synced back */
    CHECK(g.inventory.gil_full > 0);
    CHECK(g.party[0].exp > 0);         /* Cecil gained the imps* exp */
    CHECK(g.battle.active == false);
}

static void test_menu_overlay(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_SceneCtx sc;

    ff4_state_init(&g);
    strong_party(&g);
    g.world.map_id = 0;
    g.world.x = 10;
    g.world.y = 20;
    ff4_scene_init(&sc);
    ff4_scene_advance_opening(&sc);
    ff4_scene_advance_opening(&sc);
    ff4_scene_advance_opening(&sc);

    ff4_scene_request_menu(&sc);
    ff4_scene_frame(db, &g, &sc, FF4_DIR_NONE);
    CHECK(sc.scene == FF4_SCENE_MENU);

    ff4_scene_close(&sc);
    CHECK(sc.scene == FF4_SCENE_FIELD);
    ff4_scene_frame(db, &g, &sc, FF4_DIR_NONE);
    CHECK(sc.scene == FF4_SCENE_FIELD);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_field_to_battle(&db);
    test_menu_overlay(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}
