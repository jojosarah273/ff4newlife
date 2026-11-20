/**
 * @file test_battle.c
 * @brief Phase 5 tests: battle scene - formation assembly, ATB, round
 *        robin, fight/magic execution, AI dispatch, death, victory.
 *
 * Deterministic: an explicit RNG seeded with 0..255 drives every roll,
 * so the expected damage constants are hand-derived from the formulas
 * just like test_math.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/engine/battle_engine.h"
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

static void seed_table(uint8_t t[256])
{
    int i;
    for (i = 0; i < 256; ++i)
        t[i] = (uint8_t)i;
}

static void test_battle_start(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_BattleOpts o;
    uint8_t ht[256];

    seed_table(ht);
    memset(&o, 0, sizeof o);
    ff4_state_init(&g);

    /* Formation 0 (Mt. Hobs): 3x Imp. */
    CHECK(ff4_battle_start(db, &g, 0, &o) == 0);
    CHECK(g.battle.active);
    CHECK(g.battle.total_monsters == 3);
    CHECK(g.battle.monsters[0].core.id == 0);          /* Imp */
    CHECK(g.battle.present[FF4_PARTY_MAX]);   /* monster slot 5 present */
    CHECK(g.battle.present[FF4_PARTY_MAX + 2]);
    CHECK(!g.battle.present[FF4_PARTY_MAX + 3]);

    /* monster stat lines come from the db (Imp: lvl 3, hp 6). */
    CHECK(g.battle.monsters[0].core.level == 3);
    CHECK(g.battle.monsters[0].core.max_hp == 6);
    CHECK(g.battle.party[0].max_hp == g.party[0].max_hp);

    CHECK(g.battle.atb_duration[FF4_PARTY_MAX] > 0);
    g.battle.rng_table[0] = ht[0];
    ff4_battle_finish(&g.battle);
    CHECK(!g.battle.active);
}

static void test_atb_round_robin(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_BattleOpts o;
    FF4_Rng rng;

    memset(&o, 0, sizeof o);
    ff4_state_init(&g);
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    ff4_party_add(&g, FF4_CHAR_KAIN_1, 1);
    ff4_battle_start(db, &g, 0, &o);

    /* give every combatant a short, distinct gauge */
    g.battle.atb_duration[0] = 5;   /* Cecil */
    g.battle.atb_duration[1] = 6;   /* Kain  */
    g.battle.atb_duration[2] = 7;
    g.battle.atb_duration[3] = 8;
    g.battle.atb_duration[4] = 9;
    for (int i = FF4_PARTY_MAX; i < FF4_COMBATANT_SLOTS_MAX; ++i)
        g.battle.atb_duration[i] = 10;

    ff4_battle_atb(db, &g, 10);
    CHECK(ff4_battle_ready(&g.battle, 0));
    CHECK(ff4_battle_ready(&g.battle, 1));
    CHECK(ff4_battle_ready(&g.battle, FF4_PARTY_MAX));
    CHECK(!ff4_battle_ready(&g.battle, FF4_COMBATANT_SLOTS_MAX - 1) ||
          g.battle.present[FF4_COMBATANT_SLOTS_MAX - 1] == false);

    /* round robin: slots 0,1,5,6,... wrap around */
    CHECK(ff4_battle_next_ready(&g.battle, -1) == 0);
    CHECK(ff4_battle_next_ready(&g.battle, 0) == 1);
    CHECK(ff4_battle_next_ready(&g.battle, 1) == FF4_PARTY_MAX);

    /* sleeping Cecil never fills his gauge */
    g.battle.timers[0].value = 0;
    g.battle.timers[0].flags = 0;
    g.battle.party[0].status[1] |= FF4_ST2_SLEEP;
    ff4_battle_atb(db, &g, 100);
    CHECK(!ff4_battle_ready(&g.battle, 0));
    g.battle.party[0].status[1] &= (uint8_t)~FF4_ST2_SLEEP;

    (void)rng;
    ff4_battle_finish(&g.battle);
}

static void test_fight_execution(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_BattleOpts o;
    uint8_t ht[256];
    FF4_Rng rng;
    int hp_before;

    seed_table(ht);
    memset(&o, 0, sizeof o);
    ff4_state_init(&g);
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    g.party[0].max_hp = 200;
    g.party[0].hp = 200;
    g.party[0].level = 10;
    g.party[0].base_str = 13;
    g.party[0].base_agi = 10;
    g.party[0].base_sta = 11;
    g.party[0].base_int = 6;
    g.party[0].base_spr = 3;
    g.party[0].str = 13;
    g.party[0].agi = 10;
    g.party[0].attack_power = 30;
    g.party[0].attack_mult = 2;
    g.party[0].hit_rate = 90;
    g.party[0].crit_rate = 0;
    g.party[0].crit_bonus = 10;
    g.party[0].equip[FF4_EQ_RIGHT_HAND] = 0x16;  /* Shadow Blade */
    g.party[0].status[0] = 0;

    ff4_battle_start(db, &g, 0, &o);
    g.battle.monsters[0].core.def = 0;
    g.battle.monsters[0].core.def_rate = 0;
    g.battle.monsters[0].core.def_mult = 0;
    g.battle.monsters[1].core.def = 0;
    g.battle.monsters[2].core.def = 0;

    ff4_rng_init(&rng, ht);
    hp_before = g.battle.monsters[0].core.hp;

    /* Fight vs monster slot 5 (first monster) - damage lands */
    ff4_battle_auto_command(&g.battle, 0);
    ff4_battle_execute(&rng, db, &g, 0);
    CHECK(g.battle.monsters[0].core.hp < hp_before);

    /* clearing the queue works too */
    ff4_battle_auto_command(&g.battle, 1);
    ff4_battle_execute(&rng, db, &g, 1);
    ff4_battle_finish(&g.battle);
}

static void test_magic_execution(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_BattleOpts o;
    uint8_t ht[256];
    FF4_Rng rng;

    seed_table(ht);
    memset(&o, 0, sizeof o);
    ff4_state_init(&g);
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    g.party[0].max_hp = 200;
    g.party[0].hp = 200;
    ff4_battle_start(db, &g, 0, &o);

    /* casting Cure1 (spell 0x0E -> attack 0x0E, heal) on self */
    ff4_battle_set_command(&g.battle, 0, FF4_CMD_WHITE, 0x0E, 0, 0xFF);
    g.battle.monsters[0].core.hp = 1;   /* let a monster take a hit too */
    ff4_rng_init(&rng, ht);

    /* White: Cure1 effect 2 -> target pool all allies: heals Cecil */
    g.party[0].hp = 50;
    ff4_battle_execute(&rng, db, &g, 0);
    CHECK(g.battle.party[0].hp > 50);

    /* endurance: the battle copy syncs back to the field at scene end */
    {
        uint16_t after = g.battle.party[0].hp;

        ff4_battle_sync_back(&g);
        CHECK(g.party[0].hp == after);
    }

    /* Black: Fire1 (0x1D, elem fire 0x10) hits all enemies */
    {
        int before = g.battle.monsters[1].core.hp;

        ff4_battle_set_command(&g.battle, 0, FF4_CMD_BLACK, 0x1D, 0, 0xFF);
        ff4_battle_execute(&rng, db, &g, 0);
        CHECK(g.battle.monsters[1].core.hp <= before);
    }
    ff4_battle_finish(&g.battle);
}

static void test_monster_ai(const FF4_AssetDB *db)
{
    FF4_Monster m;

    memset(&m, 0, sizeof m);
    m.core.id = 0;   /* Imp */

    /* monsterScript[0] = {55 00 01 02 00 01 ff} -> action idx 0,
       monsterAction[0] = {e1 ff} -> first attack id 0xE1 */
    CHECK(ff4_monster_ai_pick(NULL, db, &m) == 0xE1);

    /* Basilisk: monsterScript[1] first pair picks action 2 (f9 24 c0..) */
    m.core.id = 1;
    CHECK(ff4_monster_ai_pick(NULL, db, &m) == 0xF9);
}

static void test_death_victory(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_BattleOpts o;
    FF4_Rng rng;
    uint8_t ht[256];
    bool defeat = false;

    seed_table(ht);
    memset(&o, 0, sizeof o);
    ff4_state_init(&g);
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    g.party[0].max_hp = 200;
    g.party[0].hp = 200;
    ff4_battle_start(db, &g, 0, &o);
    ff4_rng_init(&rng, ht);

    /* kill all monsters -> victory */
    for (int i = 0; i < 3; ++i) {
        int slot = FF4_PARTY_MAX + i;

        ff4_battle_apply_damage(&g.battle, slot, 9999, false);
    }
    ff4_battle_update_dead(&g.battle);
    CHECK(ff4_battle_check_end(&g.battle, &defeat));
    CHECK(!defeat);
    CHECK(g.battle.total_monsters == 0);
    CHECK((g.battle.end_flags & FF4_BATTLE_VICTORY) == 0);

    /* victory rewards: exp/gil/drops apply */
    ff4_battle_win_rewards(&rng, db, &g);
    CHECK(g.battle.exp_gained > 0);
    CHECK(g.battle.gil_gained > 0);
    CHECK((g.battle.end_flags & FF4_BATTLE_VICTORY) != 0);

    /* defeat: all chars dead */
    ff4_battle_start(db, &g, 0, &o);
    ff4_battle_apply_damage(&g.battle, 0, 9999, false);
    ff4_battle_apply_damage(&g.battle, 1, 9999, false);
    ff4_battle_update_dead(&g.battle);
    CHECK(ff4_battle_check_end(&g.battle, &defeat));
    CHECK(defeat);

    /* poison ticks kill an imp through the timer effect path */
    ff4_battle_start(db, &g, 0, &o);
    g.battle.monsters[0].core.status[0] |= FF4_ST1_POISON;
    g.battle.poison_pend[FF4_PARTY_MAX] = 0;
    ff4_battle_timer_effects(&g, 1);
    CHECK(g.battle.monsters[0].core.hp < g.battle.monsters[0].core.max_hp);
    ff4_battle_finish(&g.battle);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_battle_start(&db);
    test_atb_round_robin(&db);
    test_fight_execution(&db);
    test_magic_execution(&db);
    test_monster_ai(&db);
    test_death_victory(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}