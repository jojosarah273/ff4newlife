/**
 * @file test_menu.c
 * @brief Phase 7 tests: menu lists, cursor nav/wrap/scroll, inventory,
 *        magic tabs, equip filtering, shop, battle command lists,
 *        text formatting.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/engine/menu_engine.h"
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

static void test_main_menu(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_MenuCtx m;
    FF4_MenuResult r;

    ff4_state_init(&g);
    ff4_menu_open(db, &g, &m, FF4_MENU_MAIN);
    CHECK(m.open);
    CHECK(m.list.count == 8);
    CHECK(m.list.cursor == 0);

    r = ff4_menu_input(db, &g, &m, FF4_MENU_IN_DOWN);
    CHECK(r.changed && m.list.cursor == 1);

    /* wraps at the bottom: 1 + 7 = 8 -> 0 (8 rows) */
    for (int i = 0; i < 7; ++i)
        ff4_menu_input(db, &g, &m, FF4_MENU_IN_DOWN);
    CHECK(m.list.cursor == 0);

    /* confirm "Item" (value 0) enters the item screen */
    m.list.cursor = 0;
    r = ff4_menu_input(db, &g, &m, FF4_MENU_IN_CONFIRM);
    CHECK(m.screen == FF4_MENU_ITEM);
    CHECK(r.value == 0);

    /* cancel from the item screen returns to main */
    ff4_menu_input(db, &g, &m, FF4_MENU_IN_CANCEL);
    CHECK(m.screen == FF4_MENU_MAIN);

    /* cancel from main closes the menu */
    ff4_menu_input(db, &g, &m, FF4_MENU_IN_CANCEL);
    CHECK(!m.open);
}

static void test_inventory(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_MenuCtx m;

    ff4_state_init(&g);
    ff4_inventory_add(&g.inventory, FF4_ITEM_CURE1, 3);
    ff4_inventory_add(&g.inventory, FF4_ITEM_ELIXIR, 1);
    ff4_inventory_add(&g.inventory, FF4_ITEM_ETHER1, 2);

    ff4_menu_open(db, &g, &m, FF4_MENU_ITEM);
    CHECK(m.list.count == 3);
    CHECK(m.list.rows[0].value == FF4_ITEM_CURE1);
    CHECK(strstr(m.list.rows[0].text, "Cure1") != NULL);
    CHECK(strstr(m.list.rows[0].text, "x3") != NULL);
}

static void test_magic_tabs(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_MenuCtx m;
    static const uint8_t adds[][2] = {
        {3, 0x1D},   /* list 3: Fire1 */
        {4, 0x36},   /* list 4: Shiva */
        {7, 0x0E}    /* list 7: Cure1 */
    };

    ff4_state_init(&g);
    for (size_t i = 0; i < sizeof adds / sizeof adds[0]; ++i)
        g.spell_tables[adds[i][0]][0] = adds[i][1];

    /* Rydia (party 0) uses lists white/black/call via her magic slots.
       Populate directly for the menu test. */
    ff4_party_add(&g, FF4_CHAR_RYDIA_CHILD, 0);
    g.magic[0].white[0].id = FF4_SPELL_CURE1;
    g.magic[0].black[0].id = FF4_SPELL_FIRE1;
    g.magic[0].summon[0].id = FF4_SPELL_SHIVA;

    m.party_index = 0;
    m.magic_tab = 0;
    ff4_menu_open(db, &g, &m, FF4_MENU_MAGIC);
    CHECK(m.list.count == 1 && m.list.rows[0].value == FF4_SPELL_CURE1);
    CHECK(strcmp(m.list.rows[0].text, "Cure1") == 0);

    ff4_menu_input(db, &g, &m, FF4_MENU_IN_RIGHT);
    CHECK(m.magic_tab == 1);
    CHECK(m.list.count == 1 && m.list.rows[0].value == FF4_SPELL_FIRE1);

    ff4_menu_input(db, &g, &m, FF4_MENU_IN_RIGHT);
    CHECK(m.magic_tab == 2);
    CHECK(m.list.rows[0].value == FF4_SPELL_SHIVA);
    CHECK(strcmp(m.list.rows[0].text, "Shiva") == 0);

    /* from Call, right does not wrap past the last tab */
    ff4_menu_input(db, &g, &m, FF4_MENU_IN_RIGHT);
    CHECK(m.magic_tab == 2);
}

static void test_equip(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_MenuCtx m;

    ff4_state_init(&g);
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    g.party[0].job = FF4_JOB_DKNIGHT;

    /* Right-hand candidates: every listed weapon's item-class nibble must
       be covered by Cecil's mask (item_classes[1] = 0xFE3F). */
    m.party_index = 0;
    m.value = FF4_EQ_RIGHT_HAND;
    ff4_menu_open(db, &g, &m, FF4_MENU_EQUIP);
    CHECK(m.list.count > 0);
    /* Cecil's Shadow Blade is the first row (currently equipped) */
    CHECK(m.list.rows[0].text != NULL);
    {
        uint16_t mask = db->item_classes[FF4_CHAR_CECIL_DK & 0x1F];

        for (int i = 0; i < m.list.count; ++i) {
            int id = m.list.rows[i].value;
            int cls = db->equip[id].attack & 0x0F;

            if (id == 0x16)            /* his current weapon */
                continue;
            CHECK((mask & (1u << (cls + 1))) != 0);
        }
    }

    /* Body slot: the "Shadow" armor class 5 appears in no mask - the
       game's "cannot re-equip" quirk. Cecil cannot list it. */
    m.value = FF4_EQ_BODY;
    ff4_menu_open(db, &g, &m, FF4_MENU_EQUIP);
    for (int i = 0; i < m.list.count; ++i)
        CHECK(m.list.rows[i].value != 0x82);   /* Shadow Armor */

    /* Rosa (id 6, mask 0x0602): hats are the mage classes 7/8/9
       (Tiara, Ribbon). Her Iron Helm (class 4) is not offered. */
    memset(&g.party[0], 0, sizeof g.party[0]);
    g.party[0].id = FF4_CHAR_ROSA_1;
    g.party[0].job = FF4_JOB_WHITE_WIZARD;
    m.value = FF4_EQ_HEAD;
    ff4_menu_open(db, &g, &m, FF4_MENU_EQUIP);
    {
        bool saw_ribbon = false;
        uint16_t mask = db->item_classes[FF4_CHAR_ROSA_1 & 0x1F];

        for (int i = 0; i < m.list.count; ++i) {
            int id = m.list.rows[i].value;
            int cls = db->equip[id].attack & 0x0F;

            CHECK((mask & (1u << (cls + 1))) != 0);
            if (id == 0x7C)            /* Ribbon (class 9) fits Rosa */
                saw_ribbon = true;
            CHECK(id != 0x81);         /* no Iron Helm for the saint */
        }
        CHECK(saw_ribbon);             /* Rosa wears her Ribbon */
    }
}

static void test_battle_cmds(const FF4_AssetDB *db)
{
    FF4_PartyMember m;

    memset(&m, 0, sizeof m);
    m.job = FF4_JOB_DRAGOON;          /* class 1: Fight, Jump, Item */
    {
        FF4_MenuList l;

        ff4_menu_build_battle_cmds(db, &l, &m);
        CHECK(l.count == 3);
        CHECK(l.rows[0].value == FF4_CMD_FIGHT);
        CHECK(l.rows[1].value == FF4_CMD_JUMP);
        CHECK(l.rows[2].value == FF4_CMD_ITEM);
        CHECK(strcmp(l.rows[1].text, "Jump") == 0);
    }
    m.job = FF4_JOB_CALLER_CHILD;     /* class 2: Fight, White, Black, Call */
    {
        FF4_MenuList l;

        ff4_menu_build_battle_cmds(db, &l, &m);
        CHECK(l.count == 5);
        CHECK(l.rows[1].value == FF4_CMD_WHITE);
        CHECK(l.rows[3].value == FF4_CMD_CALL);
    }
    /* Cecil DK (class 0): Fight + Item. */
    m.job = FF4_JOB_DKNIGHT;
    {
        FF4_MenuList l;

        ff4_menu_build_battle_cmds(db, &l, &m);
        CHECK(l.count == 2);
        CHECK(l.rows[0].value == FF4_CMD_FIGHT);
        CHECK(l.rows[1].value == FF4_CMD_ITEM);
    }
}

static void test_shop(const FF4_AssetDB *db)
{
    FF4_GameState g;
    FF4_MenuCtx m;

    ff4_state_init(&g);
    m.value = 0;                     /* shop 0 */
    m.shop_mode = 0;                 /* buy */
    ff4_menu_open(db, &g, &m, FF4_MENU_SHOP);
    CHECK(m.list.count > 0);
    /* shop 0 first item is a Rod (0x07), per rip data */
    CHECK(m.list.rows[0].value == 0x07);

    m.shop_mode = 2;
    ff4_menu_open(db, &g, &m, FF4_MENU_SHOP);
    CHECK(m.list.count == 1);
    CHECK(m.list.rows[0].value == 0);   /* Exit */
}

static void test_format(const FF4_AssetDB *db)
{
    FF4_GameState g;
    char out[80];

    ff4_state_init(&g);
    g.inventory.gil_full = 1234;

    ff4_text_format(db, &g, "You have \\gp\\ gold.", out, sizeof out);
    CHECK(strcmp(out, "You have 1234 gold.") == 0);

    ff4_text_format(db, &g, "Line1\\nLine2", out, sizeof out);
    CHECK(strcmp(out, "Line1\nLine2") == 0);

    ff4_text_item_row(db, 0xCE, 9, out, sizeof out);
    CHECK(strcmp(out, "Cure1 x9") == 0);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_main_menu(&db);
    test_inventory(&db);
    test_magic_tabs(&db);
    test_equip(&db);
    test_battle_cmds(&db);
    test_shop(&db);
    test_format(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}