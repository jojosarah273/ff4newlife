/**
 * @file test_core.c
 * @brief Phase 1 unit tests: state structs, statuses, inventory, battle ATB
 *        and (when data/ff4-en-data.json is present) constant validation
 *        against the live rip data.
 *
 * The JSON cross-checks are the key discipline test: every enum value in
 * ff4_types.h that claims to mirror ROM data must match what `make rip`
 * extracted from the real cartridge. If the table is missing the checks
 * are skipped with a message (the engine does not *require* the data at
 * compile time; the loader does at load time).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"

#include "cJSON.h"

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
/*  Compile-time invariants                                            */
/* ------------------------------------------------------------------ */

_Static_assert(FF4_ELEM_FIRE == 0x01, "fire element bit (verified)");
_Static_assert(FF4_ELEM_ICE == 0x02, "ice element bit (verified)");
_Static_assert(FF4_ELEM_THUNDER == 0x04, "thunder element bit (verified)");
_Static_assert(FF4_ELEM_DARK == 0x08, "darkness element bit (verified)");
_Static_assert(FF4_ELEM_HOLY == 0x10, "holy element bit (verified)");
_Static_assert(FF4_ST1_DEAD == 0x80, "status byte1 dead bit");
_Static_assert(FF4_CHAR_CECIL_PALADIN == 0x0B, "paladin cecil char id");
_Static_assert(FF4_ITEM_CURE1 == 0xCE, "potion slot id (verified)");
_Static_assert(FF4_CMD_DARK == 0x05, "jp dark command id (verified)");
_Static_assert(FF4_INVENTORY_SLOTS == 48, "bag capacity");

/* ------------------------------------------------------------------ */
/*  Status flags                                                       */
/* ------------------------------------------------------------------ */

static void test_status(void)
{
    uint8_t s[4] = {0, 0, 0, 0};

    ff4_status_set(s, 0, FF4_ST1_POISON);
    ff4_status_set(s, 1, FF4_ST2_BERSERK);
    CHECK(ff4_status_has(s, 0, FF4_ST1_POISON));
    CHECK(ff4_status_has(s, 1, FF4_ST2_BERSERK));
    CHECK(!ff4_status_has(s, 0, FF4_ST1_BLIND));
    CHECK(!ff4_status_has(s, 0, FF4_ST1_DEAD));

    ff4_status_clear(s, 0, FF4_ST1_POISON);
    CHECK(!ff4_status_has(s, 0, FF4_ST1_POISON));
    CHECK(ff4_status_has(s, 1, FF4_ST2_BERSERK));

    CHECK(!ff4_status_has(s, 4, 0xFF));      /* out of range -> false */

    /* Packed view round-trips through the 4 status bytes. */
    FF4_StatusFlags packed = FF4_STATUS_PACK(0x80, 0x40, 0x20, 0x10);
    CHECK(FF4_STATUS_GET_1(packed) == 0x80);
    CHECK(FF4_STATUS_GET_2(packed) == 0x40);
    CHECK(FF4_STATUS_GET_3(packed) == 0x20);
    CHECK(FF4_STATUS_GET_4(packed) == 0x10);
}

/* ------------------------------------------------------------------ */
/*  Inventory                                                          */
/* ------------------------------------------------------------------ */

static void test_inventory(void)
{
    FF4_Inventory inv;
    int i;

    memset(&inv, 0, sizeof inv);
    CHECK(ff4_inventory_add(&inv, FF4_ITEM_CURE1, 5));
    CHECK(ff4_inventory_add(&inv, FF4_ITEM_CURE2, 1));
    CHECK(ff4_inventory_add(&inv, FF4_ITEM_CURE1, 10));
    CHECK(ff4_inventory_count(&inv) == 2);

    /* Stacking respects the 99 cap. */
    CHECK(ff4_inventory_add(&inv, FF4_ITEM_CURE1, 200));
    CHECK(inv.slots[0].item_id == FF4_ITEM_CURE1);
    CHECK(inv.slots[0].quantity == FF4_ITEM_QTY_MAX);

    CHECK(ff4_inventory_has_item(&inv, FF4_ITEM_CURE2));
    CHECK(!ff4_inventory_has_item(&inv, FF4_ITEM_ELIXIR));

    CHECK(ff4_inventory_remove(&inv, FF4_ITEM_CURE2, 1));
    CHECK(!ff4_inventory_has_item(&inv, FF4_ITEM_CURE2));
    CHECK(!ff4_inventory_remove(&inv, FF4_ITEM_ELIXIR, 1));

    /* Full bag behaviour: CURE1 occupies 1 slot; 47 more distinct items
       fill the remaining 47, and the 49th add must fail. */
    for (i = 1; i <= 47; ++i)
        CHECK(ff4_inventory_add(&inv, (FF4_ItemId)(0x01 + i * 2), 1));
    CHECK(!ff4_inventory_add(&inv, FF4_ITEM_ELIXIR, 1));
}

/* ------------------------------------------------------------------ */
/*  Party + battle                                                     */
/* ------------------------------------------------------------------ */

static void test_party_battle(void)
{
    FF4_GameState g;

    ff4_state_init(&g);
    CHECK(g.party[0].id == FF4_CHAR_NONE);

    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    ff4_party_add(&g, FF4_CHAR_KAIN_1, 1);
    CHECK(g.party[0].id == FF4_CHAR_CECIL_DK);
    CHECK(g.party[1].id == FF4_CHAR_KAIN_1);

    ff4_party_remove(&g, 1);
    CHECK(g.party[1].id == FF4_CHAR_NONE);

    /* ATB: a combatant with base_timer 10 pending after 10 frames. */
    g.battle.active = true;
    g.battle.present[0] = true;
    g.battle.present[1] = true;
    g.battle.party[0].base_timer = 10;
    g.battle.party[0].status[0] = 0;                 /* alive */
    g.battle.party[1].base_timer = 100;
    g.battle.party[1].status[0] = FF4_ST1_DEAD;      /* dead: never fills */

    ff4_battle_tick(&g.battle, 10);
    CHECK(g.battle.timers[0].value == 10);
    CHECK((g.battle.timers[0].flags & 0x01) != 0);   /* pending */
    CHECK(g.battle.timers[1].value == 0);            /* dead -> 0 */

    /* Action queue bounds. */
    FF4_PendingAction a;
    memset(&a, 0, sizeof a);
    a.actor = FF4_COMBAT_CHAR0;
    a.command = FF4_CMD_FIGHT;
    ff4_battle_queue_action(&g.battle, &a);
    ff4_battle_queue_action(&g.battle, &a);
    CHECK(g.battle.queue_size == 2);

    /* EXP/level. */
    g.party[0].level = 1;
    g.party[0].exp = 0;
    CHECK(ff4_exp_apply(&g.party[0], 100) == 1);
    CHECK(g.party[0].level == 2);
    CHECK(ff4_exp_apply(&g.party[0], 99999) > 0);
    CHECK(g.party[0].level <= FF4_LEVEL_MAX);
}

/* ------------------------------------------------------------------ */
/*  Rip-data validation (skips gracefully if data/ missing)             */
/* ------------------------------------------------------------------ */

static void test_rip_data(const char *path)
{
    cJSON *root, *obj;
    FILE *fp;
    long sz;
    char *buf;

    fp = fopen(path ? path : "data/ff4-en-data.json", "rb");
    if (!fp) {
        printf("skip: rip data unavailable (%s)\n",
               path ? path : "data/ff4-en-data.json");
        return;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return;
    }
    fclose(fp);
    buf[sz] = '\0';
    root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        printf("skip: rip data could not be parsed\n");
        return;
    }
    obj = cJSON_GetObjectItemCaseSensitive(root, "obj");
    CHECK(obj != NULL);

    /* Item ids: canonical consumables + Flame Sword slot.
       (itemNames entries are objects with an "itemName" string.) */
    {
        cJSON *names = cJSON_GetObjectItemCaseSensitive(obj, "itemNames");
        CHECK(names != NULL && cJSON_IsArray(names));
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(          /* 0xCE */
                         cJSON_GetArrayItem(names, 0xCE), "itemName")
                         ->valuestring,
                     "Cure1") == 0);
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetArrayItem(names, 0x01), "itemName")
                         ->valuestring,
                     "FireClaw") == 0);
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetArrayItem(names, 0xD3), "itemName")
                         ->valuestring,
                     "Elixir") == 0);
        /* Key item: Tower key $FA is the last one. */
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetArrayItem(names, 0xFA), "itemName")
                         ->valuestring,
                     "Tower") == 0);
    }

    /* Character + class tables. */
    {
        cJSON *cn = cJSON_GetObjectItemCaseSensitive(obj, "characterNames");
        cJSON *cl = cJSON_GetObjectItemCaseSensitive(obj, "className");
        CHECK(strcmp(cJSON_GetArrayItem(cn, 0)->valuestring, "Cecil") == 0);
        CHECK(strcmp(cJSON_GetArrayItem(cl, 0)->valuestring, "DKnight") == 0);
        CHECK(strcmp(cJSON_GetArrayItem(cl, 9)->valuestring, "Paladin") == 0);
    }

    /* Spells: Fire1 = $1D, Bahamut = $3F (spellNames entries are objects). */
    {
        cJSON *sn = cJSON_GetObjectItemCaseSensitive(obj, "spellNames");
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetArrayItem(sn, 0x1D), "spellName")
                         ->valuestring,
                     "Fire1") == 0);
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetArrayItem(sn, 0x3F), "spellName")
                         ->valuestring,
                     "Baham") == 0);
    }

    /* Commands: index 5 is Cecil's Dark in JP, "Dummy" in the US build. */
    {
        cJSON *bc = cJSON_GetObjectItemCaseSensitive(obj,
                                                     "battleCommandNames");
        CHECK(strcmp(cJSON_GetArrayItem(bc, 0x05)->valuestring, "Dummy") == 0);
        CHECK(strcmp(cJSON_GetArrayItem(bc, 0x00)->valuestring, "Fight") == 0);
    }

    /* Equipment table: FireClaw (0x01) -> ElementStatus[1] = $01 (fire),
       IceClaw (0x02) -> [2] = $02 (ice), ThunderClaw (0x03) -> [3] = $04.
       The EquipProp table is indexed by item id directly. */
    {
        cJSON *ep = cJSON_GetObjectItemCaseSensitive(obj,
                                                     "weaponArmorProperties");
        cJSON *es = cJSON_GetObjectItemCaseSensitive(obj,
                                                     "attackElementStatus");
        CHECK(ep != NULL && es != NULL);
        if (ep && es) {
        int fire = cJSON_GetObjectItemCaseSensitive(
                       cJSON_GetArrayItem(es, 1), "element")->valueint;
        int ice = cJSON_GetObjectItemCaseSensitive(
                      cJSON_GetArrayItem(es, 2), "element")->valueint;
        int thd = cJSON_GetObjectItemCaseSensitive(
                      cJSON_GetArrayItem(es, 3), "element")->valueint;
        int firestance = cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetArrayItem(ep, 0x01),
                             "elementStatus")->valueint;
        CHECK(fire == FF4_ELEM_FIRE && ice == FF4_ELEM_ICE &&
              thd == FF4_ELEM_THUNDER);
        CHECK(firestance == 1);      /* FireClaw resolves to fire element */
        }
    }

    /* Monster weakness data: Treant (idx 6) is fire-weak (weakElements=1),
       and several early undead (Skelton idx 35) carry holy (0x10). */
    {
        cJSON *mn = cJSON_GetObjectItemCaseSensitive(obj, "monsterName");
        cJSON *mp = cJSON_GetObjectItemCaseSensitive(obj,
                                                     "monsterProperties");
        int treant_weak = cJSON_GetObjectItemCaseSensitive(
                              cJSON_GetArrayItem(mp, 6),
                              "weakElements")->valueint;
        int skelton_weak = cJSON_GetObjectItemCaseSensitive(
                               cJSON_GetArrayItem(mp, 35),
                               "weakElements")->valueint;
        CHECK(strcmp(cJSON_GetArrayItem(mn, 6)->valuestring, "Treant") == 0);
        CHECK(treant_weak == FF4_ELEM_FIRE);
        CHECK((skelton_weak & FF4_ELEM_HOLY) != 0);
    }

    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    test_status();
    test_inventory();
    test_party_battle();
    test_rip_data(argc > 1 ? argv[1] : NULL);

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}