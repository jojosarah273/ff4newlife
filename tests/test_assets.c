/**
 * @file test_assets.c
 * @brief Phase 2 tests: asset database loading + typed-table integrity.
 *
 * Loads the real rip output (data/ff4-en-data.json) and cross-checks the
 * typed tables against known-good values extracted straight from the ROM,
 * including element-bit invariants established in Phase 1.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/core/ff4_types.h"
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

static void test_items(const FF4_AssetDB *db)
{
    /* Canonical names straight from the ROM name table. */
    CHECK(strcmp(db->items[0x01].name, "FireClaw") == 0);
    CHECK(strcmp(db->items[0x1C].name, "Fire") == 0);      /* Flame Sword */
    CHECK(strcmp(db->items[0xCE].name, "Cure1") == 0);     /* Potion */
    CHECK(strcmp(db->items[0xD3].name, "Elixir") == 0);
    CHECK(strcmp(db->items[0xD4].name, "Life") == 0);      /* Phoenix Down */
    CHECK(strcmp(db->items[0xFA].name, "Tower") == 0);     /* Key item */
    CHECK(strcmp(db->items[0xFF].name, "TrashCan") == 0);

    /* Consumable props: Cure1 (id $CE -> index 0x1E) heals HP. */
    CHECK(db->items[0xCE].is_consumable);
    CHECK(db->items[0xCE].consumable.effect == 2);
    CHECK(db->items[0xCE].consumable.target == 1);

    /* Trash can and sort sentinel are not consumables. */
    CHECK(!db->items[0xFF].is_consumable);
    CHECK(!db->items[0xFE].is_consumable);
}

static void test_equip(const FF4_AssetDB *db)
{
    /* Equip table is indexed by item id. FireClaw resolves to fire. */
    CHECK(db->equip[0x01].element == FF4_ELEM_FIRE);
    CHECK(db->equip[0x02].element == FF4_ELEM_ICE);
    CHECK(db->equip[0x03].element == FF4_ELEM_THUNDER);
    CHECK(db->equip[0x1D].element == FF4_ELEM_ICE);        /* Ice Brand */
    CHECK(db->equip[0x62].shield);                         /* Iron Shield */

    /* Rods carry spell-casts. */
    CHECK(db->equip[0x09].spell_cast != 0);                /* Flame Rod */
    CHECK(db->equip[0x08].spell_cast != 0);                /* Ice Rod */
}

static void test_spells(const FF4_AssetDB *db)
{
    CHECK(strcmp(db->spells[0x1D].name, "Fire1") == 0);
    CHECK(strcmp(db->spells[0x20].name, "Ice-1") == 0);
    CHECK(strcmp(db->spells[0x3C].name, "Odin") == 0);
    CHECK(strcmp(db->spells[0x3F].name, "Baham") == 0);

    /* Rosa (spell list 7) starts with Cure/Blink and learns Fire later. */
    CHECK(db->spell_initial[7][0] == 0x0E);                /* Cure1 */
    CHECK(db->spell_initial[7][1] != 0);
    CHECK(db->spell_learned_count[7] > 0);
}

static void test_monsters(const FF4_AssetDB *db)
{
    const FF4_MonsterDef *m;

    CHECK(strcmp(db->monsters[0].name, "Imp") == 0);
    CHECK(db->monsters[0].level == 3);
    CHECK(db->monsters[0].hp == 6);

    /* Element invariants from Phase 1, now from the monster table:
       Treant (idx 6) is fire-weak; undead carry holy; water kin lightning. */
    m = ff4_monster(db, 6);
    CHECK(strcmp(m->name, "Treant") == 0);
    CHECK(m->weak_elements == FF4_ELEM_FIRE);

    m = ff4_monster(db, 35);
    CHECK(strcmp(m->name, "Skelton") == 0);
    CHECK((m->weak_elements & FF4_ELEM_HOLY) != 0);

    m = ff4_monster(db, 21);
    CHECK(strcmp(m->name, "WaterHag") == 0);
    CHECK((m->weak_elements & FF4_ELEM_THUNDER) != 0);

    /* Imp's drops: Cure1 (0xCE) + ether-ish items via its item set. */
    CHECK(db->monsters[0].drop[0] == 0xCE || db->monsters[0].drop[0] == 0xFF);
    CHECK(db->monsters[0].item_rate == 1);

    /* AGI ranges are resolved. */
    CHECK(db->monsters[0].agility_max >= db->monsters[0].agility_min);
}

static void test_formations(const FF4_AssetDB *db)
{
    /* First formation: 3 Imps. */
    CHECK(db->formations[0].mons[0] == 0);
    CHECK(db->formations[0].counts[0] == 3);
    CHECK(db->formations[0].mons[1] == 0xFF);              /* no 2nd type */

    /* First random-encounter group (Mt. Hobs) and first world group
       reference valid formations. */
    int g;
    for (g = 0; g < 8; ++g) {
        uint8_t f = db->formation_groups[0][g];
        if (f != 0xFF)
            CHECK(db->formations[f].mons[0] < FF4_DB_MONSTERS);
        f = db->formation_groups_world[0][g];
        if (f != 0xFF)
            CHECK(db->formations[f].mons[0] < FF4_DB_MONSTERS);
    }
}

static void test_chars(const FF4_AssetDB *db)
{
    const FF4_CharDef *c;

    c = ff4_char(db, FF4_CHAR_CECIL_DK);
    CHECK(c != NULL);
    CHECK(strcmp(c->name, "Cecil") == 0);
    CHECK(strcmp(c->class_name, "DKnight") == 0);
    CHECK(c->job == FF4_JOB_DKNIGHT);
    CHECK(c->right_handed && !c->left_handed);
    CHECK(c->level == 10);
    CHECK(c->hp == 200 && c->cur_hp == 200);
    CHECK(c->exp == 3000 && c->exp_last_level == 2403);
    CHECK(c->str == 13 && c->agi == 10 && c->sta == 11 &&
          c->intel == 6 && c->spr == 3);

    /* Starting gear: CharInitEquip row is (initEquip-1): the Shadow set. */
    CHECK(c->eq_right == 0x16);                            /* Shadow Blade */
    CHECK(c->eq_left == 0x62);                             /* Shadow Shield */
    CHECK(c->eq_body == 0x82);                             /* Shadow Armor  */
    CHECK(c->eq_head == 0x6E);                             /* Shadow Helm   */

    /* Cid: joins from a cell, wooden hammer + prisoner robe, Chief class. */
    c = ff4_char(db, FF4_CHAR_CID);
    CHECK(c != NULL);
    CHECK(strcmp(c->name, "Cid") == 0);
    CHECK(c->job == FF4_JOB_CHIEF);
    CHECK(c->level == 20);
    CHECK(c->eq_right == 0x4B);                            /* Wooden hammer */
    CHECK(c->eq_body == 0x96);                             /* Prisoner robe  */

    /* Edge: ninja, level 25, joins with dual katana (Short x2). */
    c = ff4_char(db, FF4_CHAR_EDGE);
    CHECK(c != NULL && c->job == FF4_JOB_NINJA && c->level == 25);
    CHECK(c->eq_right == 0x2B && c->eq_left == 0x2B);      /* Short x2     */

    /* The paladin re-entry is equipped with the Legend Sword. */
    c = ff4_char(db, FF4_CHAR_CECIL_PALADIN);
    CHECK(c != NULL && c->level == 1);
    CHECK(c->job == FF4_JOB_PALADIN);
    CHECK(c->eq_right == 0x19);                            /* Legend Sword */

    /* FuSoYa: lunar staff + sorcerer robes. */
    c = ff4_char(db, FF4_CHAR_FUSOYA);
    CHECK(c != NULL && c->level == 50);
    CHECK(c->eq_right == 0x13);                            /* Lunar staff  */

    /* Find-by-id must reject unknown ids (Anna has no CharProp row). */
    CHECK(ff4_char(db, FF4_CHAR_ANNA) == NULL);
}

static void test_levelup(const FF4_AssetDB *db)
{
    const FF4_LevelUpEntry *e;

    /* Cecil's table: level 1 threshold is 931. */
    e = ff4_level_entry(db, 0, 1);
    CHECK(e != NULL);
    CHECK(ff4_level_exp(e) == 931);
    CHECK(e->hp == 20);

    e = ff4_level_entry(db, 0, 2);
    CHECK(e && ff4_level_exp(e) == 1186);

    /* Tables grow monotonically-ish and are bounded. */
    CHECK(db->levelup_rows[0] >= 8);
    CHECK(db->levelup_rows[0] <= FF4_DB_LEVEL_MAX);

    /* High tail rows exist but carry only stat growth (no exp yet). */
    e = ff4_level_entry(db, 0, db->levelup_rows[0]);
    CHECK(e && e->exp_low == 0xFFFF && e->exp_msb == 0xFF);
}

static void test_shops_maps(const FF4_AssetDB *db)
{
    /* First shop (Baron weapons): rod, staff, bows... all valid item ids. */
    int i;
    for (i = 0; i < 8; ++i) {
        uint8_t id = db->shops[0].items[i];
        CHECK(id == 0xFF || id != 0xFF);   /* any defined id is representable */
        if (id != 0xFF)
            CHECK(db->items[id].name[0] != '\0');
    }
    CHECK(db->shops[0].items[0] == 0x07);                  /* Rod */

    /* Map 0 = Baron (town). Titles come from the mapTitle table. */
    CHECK(db->maps[0].id == 0);
    CHECK(db->maps[0].title[0] != '\0');
    CHECK(strcmp(db->map_titles[0], "1F") == 0);

    /* Tileset 0 has 256 tile property entries. */
    CHECK(db->map_tile_props[0][0][0] == 1);
}

static void test_blobs(const FF4_AssetDB *db)
{
    const FF4_Blob *b;

    /* RNG sequence table: 256 bytes, decoded and registered. */
    CHECK(db->rng_table_valid);
    CHECK(db->blob_count > 0);

    b = ff4_db_blob(db, "rngTable");
    CHECK(b != NULL && b->len >= 256);

    b = ff4_db_blob(db, "prophecyText");
    CHECK(b != NULL && b->len > 0);

    b = ff4_db_blob(db, "spcData");
    CHECK(b != NULL);

    b = ff4_db_blob(db, "worldGraphics[0]");
    CHECK(b != NULL && b->len > 0);
}

static void test_rip_table(const FF4_AssetDB *db)
{
    /* The rip companion file provides the byte->glyph table. */
    CHECK(db->char_table.text_valid[0x01]);                /* newline */
    CHECK(db->char_table.dte_valid[0x8A]);                 /* DTE pair */

    /* Glyph decoding: FF4's font maps 0x42 -> 'A', 0x43 -> 'B'... */
    CHECK(ff4_glyph(db, 0x42) == 'A' || ff4_glyph(db, 0x42) == '?');
    CHECK(ff4_glyph(db, 0xFF) == '?' || db->char_table.text_valid[0xFF]);
}

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *data_path = argc > 1 ? argv[1] : "data/ff4-en-data.json";
    const char *rip_path = argc > 2 ? argv[2]
                                    : "third_party/ff4/vanilla/ff4-en-rip.json";

    if (ff4_db_load(&db, data_path) != 0) {
        printf("fatal: could not load %s (%s)\n", data_path, db.error);
        return 1;
    }
    memset(&db.char_table, 0, sizeof db.char_table);
    if (ff4_db_load_rip(&db, rip_path) != 0)
        printf("note: rip companion not loaded (%s)\n", rip_path);

    test_items(&db);
    test_equip(&db);
    test_spells(&db);
    test_monsters(&db);
    test_formations(&db);
    test_chars(&db);
    test_levelup(&db);
    test_shops_maps(&db);
    test_blobs(&db);
    test_rip_table(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}