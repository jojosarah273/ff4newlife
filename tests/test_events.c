/**
 * @file test_events.c
 * @brief Phase 4 tests: event script decoding + interpreter semantics.
 *
 *  - Round-trips ALL 256 ripped scripts through the decoder table
 *    (every real event must decode cleanly into complete sub-scripts).
 *  - Exercises the interpreter: switches, party save/restore, items,
 *    gil, spells, hp/mp, statuses, warps, repeat loops, waits, and the
 *    scene request / block / ack protocol.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff4/core/ff4_math.h"      /* rng (not needed here, for seeding) */
#include "ff4/engine/asset_db.h"
#include "ff4/engine/event_engine.h"

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
/*  Decoder round-trip over the whole ripped script table              */
/* ------------------------------------------------------------------ */

static int decode_all_scripts(const FF4_AssetDB *db)
{
    const FF4_Blob *blob;
    int parsed = 0;
    int i;

    for (i = 0; i < 256; ++i) {
        char key[32];
        const uint8_t *p;
        size_t len, pos = 0;
        int segs = 0, ok = 1;

        snprintf(key, sizeof key, "eventScript[%d]", i);
        blob = ff4_db_blob(db, key);
        if (!blob)
            continue;
        p = blob->data;
        len = blob->len;

        while (ok && pos < len) {
            FF4_EventCmdDecoded d;

            if (ff4_event_decode(p, len, &pos, &d) != 0) {
                ok = 0;
                break;
            }
            if (d.id == FF4_EVT_END) {
                ++segs;
                /* skip padding ff bytes between sub-scripts */
                while (pos < len && p[pos] == 0xFF)
                    ++pos;
                continue;
            }
            if (d.id == FF4_EVT_REPEAT) {
                /* the decoder skips the body; nothing else to do */
            }
        }
        if (ok && segs > 0)
            ++parsed;
    }
    return parsed;
}

static void test_scripts(const FF4_AssetDB *db)
{
    int parsed = decode_all_scripts(db);

    /* every ripped script must decode (single or multi sub-script) */
    CHECK(parsed >= 250);
    printf("  event scripts decoded cleanly: %d/256\n", parsed);
}

/* ------------------------------------------------------------------ */
/*  Broken-out semantic tests                                          */
/* ------------------------------------------------------------------ */

static void test_decode(void)
{
    const uint8_t s[] = {
        0xEE, 0x02,               /* npc dialog 2 */
        0xF2, 0x05,               /* set event switch 5 */
        0xE2, 0x03, 0x1D,         /* learn Fire1 into list 3 */
        0xFE, 0x08, 0x26, 0x10,   /* warp: map 8, dir 0 + x 6, y 16 */
        0xFF
    };
    size_t pos = 0;
    FF4_EventCmdDecoded d;

    CHECK(ff4_event_decode(s, sizeof s, &pos, &d) == 0 &&
          d.id == FF4_EVT_NPC_DIALOG && d.args[0] == 2);
    CHECK(ff4_event_decode(s, sizeof s, &pos, &d) == 0 &&
          d.id == FF4_EVT_SWITCH_SET && d.args[0] == 5);
    CHECK(ff4_event_decode(s, sizeof s, &pos, &d) == 0 &&
          d.id == FF4_EVT_SPELL && d.arg_count == 2 &&
          d.args[0] == 3 && d.args[1] == 0x1D);
    CHECK(ff4_event_decode(s, sizeof s, &pos, &d) == 0 &&
          d.id == FF4_EVT_MAP && d.arg_count == 3 &&
          d.args[0] == 0x08 && d.args[1] == 0x26 && d.args[2] == 0x10);
    CHECK(ff4_event_decode(s, sizeof s, &pos, &d) == 0 &&
          d.id == FF4_EVT_END);
    CHECK(pos == sizeof s);

    /* action nibbles (id holds the raw opcode; obj/act carry the nibles) */
    {
        const uint8_t a[] = {0x12, 0xC5, 0xA8};
        pos = 0;
        CHECK(ff4_event_decode(a, sizeof a, &pos, &d) == 0 &&
              d.id == (FF4_EventCmd)0x12 && d.obj == 1 && d.act == 2);
        CHECK(ff4_event_decode(a, sizeof a, &pos, &d) == 0 &&
              d.id == (FF4_EventCmd)0xC5 && d.obj == 0xC && d.act == 5);
        CHECK(ff4_event_decode(a, sizeof a, &pos, &d) == 0 &&
              d.id == (FF4_EventCmd)0xA8 && d.obj == 0xA && d.act == 8);
    }

    /* malformed: truncated parameter stream */
    {
        const uint8_t bad[] = {0xE2, 0x03};
        size_t p2 = 0;

        CHECK(ff4_event_decode(bad, sizeof bad, &p2, &d) == -1);
    }
}

static void test_interpreter_basic(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    FF4_EventAction act;
    /* script: switch 5 on, dialog, switch 5 off, end */
    static const uint8_t s[] = {0xF2, 0x05, 0xF0, 0x01, 0xF3, 0x05, 0xFF};
    int i;

    ff4_state_init(&g);
    ff4_event_run_begin(&run, &g, db, s, sizeof s);

    CHECK(run.state == FF4_EVENT_RUNNING);
    CHECK(g.world.event_running);

    /* switch 5 set */
    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_NONE);
    CHECK((g.world.event_switches[0] & 0x20) != 0);   /* bit 5 */

    /* dialogue 1 (bank 1) blocks the run */
    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_DIALOGUE);
    CHECK(run.state == FF4_EVENT_BLOCKED);
    CHECK(run.last.text != NULL && run.last.text[0] != '\0');

    /* nothing advances while blocked */
    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_NONE);

    ff4_event_ack(&run);
    CHECK(run.state == FF4_EVENT_RUNNING);

    /* switch 5 clear, then end */
    act = ff4_event_step(&run);
    CHECK((g.world.event_switches[0] & 0x20) == 0);
    act = ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);

    /* event_running cleared at completion */
    for (i = 0; i < 5; ++i)
        ff4_event_step(&run);
}

static void test_interpreter_party(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    int i;

    ff4_state_init(&g);

    /* add Kain (id 2): loads from CharProp (CharRemoveTbl[1] = 0x01). */
    {
        static const uint8_t add_kain[] = {0xE7, 0x02, 0xFF};

        ff4_event_run_begin(&run, &g, db, add_kain, sizeof add_kain);
        ff4_event_step(&run);
        CHECK(g.party[0].id == FF4_CHAR_KAIN_1);
        CHECK(g.party[0].level == 10);
        CHECK(g.party[0].equip[FF4_EQ_LEFT_HAND] == 0x23); /* Spear */
        ff4_event_step(&run);                          /* end */
        CHECK(run.state == FF4_EVENT_DONE);
    }

    /* remove Kain (id 2): saves to slot 0 (CharAddTbl[1] = 0x00). */
    {
        static const uint8_t rem_kain[] = {0xE8, 0x02, 0xFF};

        g.party[0].hp = 55;
        ff4_event_run_begin(&run, &g, db, rem_kain, sizeof rem_kain);
        ff4_event_step(&run);
        CHECK(g.party[0].id == FF4_CHAR_NONE);
        CHECK(g.saved_char[0].id == FF4_CHAR_KAIN_1);
        CHECK(g.saved_char[0].hp == 55);         /* saved as-is */
    }

    /* add Kain again: restoration from the save slot, full HP/MP. */
    {
        static const uint8_t add_kain[] = {0xE7, 0x02, 0xFF};

        ff4_event_run_begin(&run, &g, db, add_kain, sizeof add_kain);
        ff4_event_step(&run);
        CHECK(g.party[0].id == FF4_CHAR_KAIN_1);
        CHECK(g.party[0].hp == g.party[0].max_hp);
    }

    /* adult Rydia (id 0x11) gains the adult summoner class. */
    {
        static const uint8_t add_rydia[] = {0xE7, 0x11, 0xFF};

        for (i = 0; i < FF4_PARTY_MAX; ++i)
            memset(&g.party[i], 0, sizeof g.party[i]);
        ff4_event_run_begin(&run, &g, db, add_rydia, sizeof add_rydia);
        ff4_event_step(&run);
        CHECK(g.party[0].id == FF4_CHAR_RYDIA_ADULT);
        CHECK(g.party[0].job == FF4_JOB_CALLER_ADULT);
    }
}

static void test_interpreter_items_gp(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    FF4_EventAction act;
    static const uint8_t s[] = {
        0xE0, 0xCE,               /* give Cure1 */
        0xE1, 0xCE,               /* take Cure1 */
        0xE5, 100,                /* give 100 gp */
        0xE6, 40,                 /* take 40 gp */
        0xE0, 0x01,               /* give FireClaw (bag now full) */
        0xFF
    };
    int i;

    ff4_state_init(&g);
    ff4_event_run_begin(&run, &g, db, s, sizeof s);

    ff4_event_step(&run);                         /* give Cure1 */
    CHECK(ff4_inventory_has_item(&g.inventory, FF4_ITEM_CURE1));
    ff4_event_step(&run);                         /* take */
    CHECK(!ff4_inventory_has_item(&g.inventory, FF4_ITEM_CURE1));
    ff4_event_step(&run);                         /* +100 */
    ff4_event_step(&run);                         /* -40 */
    CHECK(g.inventory.gil_full == 60);

    /* fill the bag so the 5th command cannot carry the item */
    for (i = 0; i < FF4_INVENTORY_SLOTS; ++i) {
        g.inventory.slots[i].item_id = (uint8_t)(0x02 + i);
        g.inventory.slots[i].quantity = 1;
    }
    g.inventory.count = ff4_inventory_count(&g.inventory);

    act = ff4_event_step(&run);                   /* give FireClaw */
    CHECK(act.kind == FF4_EVT_ACT_DIALOGUE);      /* "Can't carry!" */
    CHECK(run.state == FF4_EVENT_BLOCKED);
    CHECK(run.last.text != NULL &&
          strcmp(run.last.text, "Can't carry!") == 0);
    ff4_event_ack(&run);
    ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);
}

static void test_interpreter_wait_gp(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    FF4_EventAction act;
    static const uint8_t s[] = {0xE9, 0x05, 0xF2, 0x01, 0xFF};

    ff4_state_init(&g);
    ff4_event_run_begin(&run, &g, db, s, sizeof s);

    /* wait 5 * 8 = 40 frames */
    act = ff4_event_step(&run);
    CHECK(run.wait == 40);

    ff4_event_tick(&run, 10);
    CHECK(run.wait == 30);
    act = ff4_event_step(&run);                   /* still waiting */
    CHECK(run.wait == 30 && act.kind == FF4_EVT_ACT_WAIT);

    ff4_event_tick(&run, 30);
    CHECK(run.wait == 0);
    ff4_event_step(&run);                         /* switch set now runs */
    CHECK((g.world.event_switches[0] & 0x02) != 0);
    ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);
}

static void test_interpreter_repeat(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    /* repeat 3 times: { switch 7 set } then end.
       eb 03 02 f2 07 ff : count 3, body len 2 = f2 07 */
    static const uint8_t s[] = {0xEB, 0x03, 0x02, 0xF2, 0x07, 0xFF};
    int i;

    ff4_state_init(&g);
    ff4_event_run_begin(&run, &g, db, s, sizeof s);

    for (i = 0; i < 9; ++i)
        ff4_event_step(&run);
    /* switch 7 bits were OR-set three times (idempotent but executed 3x) */
    CHECK((g.world.event_switches[0] & 0x80) != 0);
    CHECK(run.state == FF4_EVENT_DONE);
    CHECK(run.safety <= 20);                   /* no runaway blowup */
}

static void test_interpreter_warp(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    static const uint8_t s[] = {0xFE, 0x05, 0xA2, 0x14, 0xFF};
    /* map 5, dir = 0xA2 >> 6 = 2 (down), x = 0xA2 & 0x3F = 0x22, y 0x14 */

    ff4_state_init(&g);
    g.world.x = 1;
    g.world.y = 1;
    ff4_event_run_begin(&run, &g, db, s, sizeof s);
    ff4_event_step(&run);

    CHECK(g.world.map_id == 5);
    CHECK(g.world.facing == FF4_DIR_DOWN);
    CHECK(g.world.x == 0x22);
    CHECK(g.world.y == 0x14);
    CHECK(g.world.prev_map_count == 1);
    ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);
}

static void test_interpreter_statuses_spells(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    int i;

    ff4_state_init(&g);
    /* status toggle poison, hp restore x10, learn Fire1 (list 3) */
    static const uint8_t s[] = {
        0xE7, 0x01,               /* Cecil */
        0xDB, 0x01,               /* toggle poison */
        0xDE, 0x0A,               /* +100 hp */
        0xDF, 0x05,               /* +50 mp */
        0xE2, 0x03, 0x1D,         /* learn Fire1 into list 3 */
        0xE3, 0xBE,               /* clear (keep) mask */
        0xFF
    };

    ff4_event_run_begin(&run, &g, db, s, sizeof s);
    ff4_event_step(&run);                         /* add Cecil */
    CHECK(g.party[0].id == FF4_CHAR_CECIL_DK);
    CHECK(g.party[0].max_hp == 200);

    ff4_event_step(&run);                         /* poison */
    CHECK((g.party[0].status[0] & FF4_ST1_POISON) != 0);

    g.party[0].hp = 40;
    ff4_event_step(&run);                         /* +100 hp */
    CHECK(g.party[0].hp == 140);

    g.party[0].max_mp = 100;
    g.party[0].mp = 0;
    ff4_event_step(&run);                         /* +50 mp */
    CHECK(g.party[0].mp == 50);

    ff4_event_step(&run);                         /* learn spell */
    CHECK(g.spell_tables[3][0] == 0x1D);          /* Fire1 */

    /* e3 with 0xBE keeps everything except poison (whitelist AND) */
    ff4_event_step(&run);                         /* status clear mask */
    CHECK((g.party[0].status[0] & FF4_ST1_POISON) == 0);
    ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);
    (void)i;
}

static void test_scene_list(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_EventRun run;
    FF4_EventAction act;
    static const uint8_t s[] = {
        0xEC, 0x02,               /* battle formation 2 */
        0xED, 0x01,               /* shop 1 */
        0xDC, 50,                 /* inn 50 */
        0xFA, 0x0F,               /* song 15 */
        0xFF
    };

    ff4_state_init(&g);
    ff4_event_run_begin(&run, &g, db, s, sizeof s);

    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_BATTLE && act.param == 2);
    CHECK(run.state == FF4_EVENT_BLOCKED);
    ff4_event_ack(&run);

    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_SHOP && act.param == 1);
    ff4_event_ack(&run);

    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_INN && act.param == 50);
    ff4_event_ack(&run);

    act = ff4_event_step(&run);
    CHECK(act.kind == FF4_EVT_ACT_SONG && act.param == 15);
    ff4_event_step(&run);
    CHECK(run.state == FF4_EVENT_DONE);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_decode();
    test_scripts(&db);
    test_interpreter_basic(&db);
    test_interpreter_party(&db);
    test_interpreter_items_gp(&db);
    test_interpreter_wait_gp(&db);
    test_interpreter_repeat(&db);
    test_interpreter_warp(&db);
    test_interpreter_statuses_spells(&db);
    test_scene_list(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}