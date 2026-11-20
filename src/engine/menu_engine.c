/**
 * @file menu_engine.c
 * @brief Phase 7: menu engine - cursor lists, screens, formatting.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/menu_engine.h"

/* ==================================================================== */
/*  List navigation                                                     */
/* ==================================================================== */

static int list_first_enabled(const FF4_MenuList *l)
{
    int i;

    for (i = 0; i < l->count; ++i)
        if (l->rows[i].enabled)
            return i;
    return l->count ? 0 : -1;
}

static int row_enabled(const FF4_MenuList *l, int i)
{
    return i >= 0 && i < l->count && l->rows[i].enabled;
}

static int list_move(FF4_MenuList *l, int delta)
{
    int cur = l->cursor;

    if (l->count <= 1)
        return l->cursor;
    do {
        cur += delta;
        if (cur >= l->count)
            cur = 0;                       /* wrap (the game wraps lists) */
        if (cur < 0)
            cur = l->count - 1;
    } while (!row_enabled(l, cur) && cur != l->cursor);
    l->cursor = cur;

    /* keep the cursor inside the visible page */
    if (cur < l->top)
        l->top = cur;
    if (cur >= l->top + (l->page > 0 ? l->page : 1))
        l->top = cur - (l->page > 0 ? l->page : 1) + 1;
    if (l->page > 0 && l->top + l->page > l->count)
        l->top = l->count - l->page > 0 ? l->count - l->page : 0;
    return l->cursor;
}

/* ==================================================================== */
/*  Builders                                                            */
/* ==================================================================== */

static void lrow(FF4_MenuList *l, int i, const char *text, int value,
                 bool enabled)
{
    if (i >= 0 && i < FF4_MENU_MAX_ROWS) {
        /* copy into the list-owned arena so the text outlives the
           builder's stack frame (snprintf is safe: src != dst) */
        snprintf(l->arena[i], sizeof l->arena[i], "%s", text);
        l->rows[i].text = l->arena[i];
        l->rows[i].value = value;
        l->rows[i].enabled = enabled;
        if (i >= l->count)
            l->count = i + 1;
    }
}

static void ltitle(FF4_MenuList *l, const char *title)
{
    snprintf(l->title_buf, sizeof l->title_buf, "%s", title);
    l->title = l->title_buf;
}

void ff4_menu_build_main(FF4_MenuList *l)
{
    static const char *items[] = {
        "Item", "Magic", "Equip", "Status",
        "Order", "Config", "Save", "Exit"
    };
    int i;

    memset(l, 0, sizeof *l);
    l->page = 8;
    l->title = "Main";
    for (i = 0; i < 8; ++i)
        lrow(l, i, items[i], i, true);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_inventory(const FF4_AssetDB *db, const FF4_GameState *g,
                              FF4_MenuList *l)
{
    int i, n = 0;

    memset(l, 0, sizeof *l);
    l->page = FF4_MENU_PAGE_ITEMS;
    ltitle(l, "Item");
    for (i = 0; i < FF4_INVENTORY_SLOTS && n < FF4_MENU_MAX_ROWS; ++i) {
        const FF4_InventorySlot *slot = &g->inventory.slots[i];

        char tmp[32];

        if (slot->quantity == 0)
            continue;
        ff4_text_item_row(db, slot->item_id, slot->quantity,
                          tmp, sizeof tmp);
        lrow(l, n, tmp, slot->item_id, true);
        ++n;
    }
    l->count = n;
    if (l->count == 0)
        lrow(l, 0, "(empty)", 0, false);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_magic(const FF4_AssetDB *db, const FF4_GameState *g,
                          FF4_MenuList *l, int party_index, int tab)
{
    const FF4_SpellSlot *slots;
    int i, n = 0;

    memset(l, 0, sizeof *l);
    l->page = 9;
    ltitle(l, tab == 0 ? "White" : (tab == 1 ? "Black" : "Call"));

    /* read the member's learned lists (field copy, $1560-based). */
    if (party_index >= 0 && party_index < FF4_PARTY_MAX) {
        slots = tab == 0 ? g->magic[party_index].white
              : tab == 1 ? g->magic[party_index].black
                         : g->magic[party_index].summon;
    } else {
        slots = NULL;
    }
    for (i = 0; slots && i < FF4_SPELLS_PER_LIST && n < FF4_MENU_MAX_ROWS;
         ++i) {
        uint8_t spell = slots[i].id;

        if (!spell)
            continue;
        lrow(l, n, spell < FF4_DB_SPELLS ? db->spells[spell].name : "?",
             spell, true);
        ++n;
    }
    l->count = n;
    if (n == 0)
        lrow(l, 0, "(none)", 0, false);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_equip(const FF4_AssetDB *db, const FF4_GameState *g,
                          FF4_MenuList *l, int party_index, int slot)
{
    static const char *slot_names[] = {
        "Right Hand", "Left Hand", "Head", "Body", "Fingers"
    };
    const FF4_PartyMember *m = &g->party[party_index];
    uint8_t min_id = 0x01, max_id = 0x60;
    int i, n = 0;

    memset(l, 0, sizeof *l);
    l->page = 9;
    ltitle(l, slot_names[slot]);

    switch (slot) {
    case FF4_EQ_HEAD:   min_id = 0x77; max_id = 0x8C; break;
    case FF4_EQ_BODY:   min_id = 0x8D; max_id = 0xA6; break;
    case FF4_EQ_FINGER: min_id = 0xA7; max_id = 0xAF; break;
    default:            min_id = 0x01; max_id = 0x60; break;
    }

    /* current equipment first */
    if (m->equip[slot]) {
        lrow(l, n, ff4_item_name(db, m->equip[slot]), m->equip[slot], true);
        ++n;
    }
    /* candidates: within the slot's id range whose item class nibble
       (equip row byte1 & 0x0F) is allowed by the character's mask.
       The mask lives in item_classes[char id] - class-5 gear appears in
       no mask, reproducing the game's "Shadow equipment cannot be
       re-equipped" quirk (menu/equip.asm @c2a0). */
    for (i = min_id; i <= max_id && n < 8; ++i) {
        const FF4_EquipDef *eq;
        uint16_t mask;
        int cls;

        if ((int)i > FF4_DB_EQUIP)
            break;
        eq = &db->equip[i];
        cls = eq->attack & 0x0F;
        mask = db->item_classes[m->id & 0x1F];
        if (!mask || !(mask & (1u << (cls + 1))))
            continue;
        lrow(l, n, db->items[i].name, i, true);
        ++n;
    }
    if (n == 0)
        lrow(l, 0, "(none)", 0, false);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_battle_cmds(const FF4_AssetDB *db, FF4_MenuList *l,
                                const FF4_PartyMember *m)
{
    int cls = m->job;
    int i, n = 0;

    memset(l, 0, sizeof *l);
    l->page = 5;
    ltitle(l, "Command");
    if (cls >= 0 && cls < 22) {
        for (i = 0; i < 5; ++i) {
            uint8_t cmd = db->class_commands[cls][i];

            if (cmd == 0xFF)
                continue;
            lrow(l, n, db->battle_command_names[cmd], cmd, true);
            ++n;
        }
    }
    if (n == 0)
        lrow(l, 0, "(none)", 0, false);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_shop(const FF4_AssetDB *db, FF4_MenuList *l, int shop,
                         int mode)
{
    int i, n = 0;

    memset(l, 0, sizeof *l);
    l->page = 8;
    ltitle(l, mode == 0 ? "Buy" : (mode == 1 ? "Sell" : "Shop"));

    if (mode <= 1) {
        for (i = 0; i < 8; ++i) {
            uint8_t id = db->shops[shop].items[i];

            if (id == 0xFF)
                continue;
            lrow(l, n, ff4_item_name(db, id), id, true);
            ++n;
        }
    } else {
        lrow(l, n, "Exit", 0, true);
        ++n;
    }
    if (n == 0)
        lrow(l, 0, "(closed)", 0, false);
    l->cursor = list_first_enabled(l);
}

void ff4_menu_build_confirm(FF4_MenuList *l, const char *prompt)
{
    memset(l, 0, sizeof *l);
    l->page = 2;
    ltitle(l, prompt ? prompt : "OK?");
    lrow(l, 0, "Yes", 1, true);
    lrow(l, 1, "No", 0, true);
    l->cursor = list_first_enabled(l);
}

/* ==================================================================== */
/*  Screen open + input                                                 */
/* ==================================================================== */

void ff4_menu_open(const FF4_AssetDB *db, FF4_GameState *g, FF4_MenuCtx *m,
                   FF4_MenuScreen screen)
{
    m->screen = screen;
    m->open = true;

    switch (screen) {
    case FF4_MENU_MAIN:   ff4_menu_build_main(&m->list); break;
    case FF4_MENU_ITEM:   ff4_menu_build_inventory(db, g, &m->list); break;
    case FF4_MENU_MAGIC:  ff4_menu_build_magic(db, g, &m->list,
                                               m->party_index, m->magic_tab);
                          break;
    case FF4_MENU_EQUIP:  ff4_menu_build_equip(db, g, &m->list,
                                               m->party_index, m->value);
                          break;
    case FF4_MENU_STATUS: ff4_menu_build_confirm(&m->list, "Status");
                          m->list.rows[0].text = "Lv status"; break;
    case FF4_MENU_BATTLE_CMD:
        ff4_menu_build_battle_cmds(db, &m->list,
                                   &g->battle.party[m->party_index]);
        break;
    case FF4_MENU_SHOP:   ff4_menu_build_shop(db, &m->list, m->value,
                                              m->shop_mode);
                          break;
    case FF4_MENU_CONFIRM:ff4_menu_build_confirm(&m->list, "OK?"); break;
    case FF4_MENU_TARGET: break;    /* built dynamically per battle */
    default:              m->open = false; break;
    }
}

FF4_MenuResult ff4_menu_input(const FF4_AssetDB *db, FF4_GameState *g,
                              FF4_MenuCtx *m, FF4_MenuInput input)
{
    FF4_MenuResult res;

    memset(&res, 0, sizeof res);
    if (!m->open)
        return res;

    switch (input) {
    case FF4_MENU_IN_UP:
        if (m->screen == FF4_MENU_TARGET) {
            res.changed = true;
        } else {
            list_move(&m->list, -1);
            res.changed = true;
        }
        break;
    case FF4_MENU_IN_DOWN:
        if (m->screen == FF4_MENU_TARGET) {
            res.changed = true;
        } else {
            list_move(&m->list, 1);
            res.changed = true;
        }
        break;
    case FF4_MENU_IN_LEFT:
        if (m->screen == FF4_MENU_MAGIC && m->magic_tab > 0) {
            m->magic_tab--;
            ff4_menu_build_magic(db, g, &m->list, m->party_index,
                                 m->magic_tab);
            res.changed = true;
        } else if (m->screen == FF4_MENU_SHOP && m->shop_mode > 0) {
            m->shop_mode--;
            ff4_menu_build_shop(db, &m->list, m->value, m->shop_mode);
            res.changed = true;
        }
        break;
    case FF4_MENU_IN_RIGHT:
        if (m->screen == FF4_MENU_MAGIC && m->magic_tab < 2) {
            m->magic_tab++;
            ff4_menu_build_magic(db, g, &m->list, m->party_index,
                                 m->magic_tab);
            res.changed = true;
        } else if (m->screen == FF4_MENU_SHOP && m->shop_mode < 2) {
            m->shop_mode++;
            ff4_menu_build_shop(db, &m->list, m->value, m->shop_mode);
            res.changed = true;
        }
        break;
    case FF4_MENU_IN_CONFIRM:
        if (m->list.count > 0 && m->list.cursor < m->list.count) {
            res.changed = true;
            res.done = m->screen == FF4_MENU_MAIN ||
                       m->screen == FF4_MENU_BATTLE_CMD ||
                       m->screen == FF4_MENU_SELECT;
            res.value = m->list.rows[m->list.cursor].value;

            /* main menu items descend into their screens */
            if (m->screen == FF4_MENU_MAIN) {
                switch (res.value) {
                case 0: ff4_menu_open(db, g, m, FF4_MENU_ITEM); break;
                case 1: m->party_index = 0;
                        ff4_menu_build_magic(db, g, &m->list, 0, 0);
                        break;
                case 2: m->party_index = 0; m->value = FF4_EQ_RIGHT_HAND;
                        ff4_menu_open(db, g, m, FF4_MENU_EQUIP); break;
                case 7: m->open = false; break;
                default: break;
                }
            }
        }
        break;
    case FF4_MENU_IN_CANCEL:
        res.changed = true;
        if (m->screen == FF4_MENU_MAIN)
            m->open = false;             /* close the whole menu */
        else
            ff4_menu_open(db, g, m, FF4_MENU_MAIN);   /* one level up */
        break;
    }
    return res;
}

/* ==================================================================== */
/*  Text formatting                                                     */
/* ==================================================================== */

size_t ff4_text_item_row(const FF4_AssetDB *db, uint8_t item, uint8_t qty,
                         char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s", ff4_item_name(db, item));

    if (qty > 1 && n > 0 && (size_t)n < cap)
        n += snprintf(out + n, cap - (size_t)n, " x%u", qty);
    return (size_t)n;
}

size_t ff4_text_format(const FF4_AssetDB *db, const FF4_GameState *g,
                       const char *src, char *out, size_t cap)
{
    size_t o = 0;
    const char *p;

    (void)db;
    for (p = src; *p && o + 1 < cap; ++p) {
        /* multi-char tokens the rip uses: \gp\ (gil), \pad, \item\ */
        if (p[0] == '\\' && p[1] == 'g' && p[2] == 'p' && p[3] == '\\') {
            int n = snprintf(out + o, cap - o, "%u",
                             (unsigned)g->inventory.gil_full);

            if (n > 0)
                o += (size_t)n;
            p += 3;
            continue;
        }
        if (p[0] == '\\' && p[1] == 'p' && p[2] == 'a' && p[3] == 'd') {
            p += 3;                         /* pad: renderer expands */
            continue;
        }
        if (*p != '\\' || p[1] == '\0') {
            out[o++] = *p;
            continue;
        }
        ++p;
        switch (*p) {
        case 'n': out[o++] = '\n'; break;
        default:
            out[o++] = *p;
            break;
        }
    }
    out[o] = '\0';
    return o;
}