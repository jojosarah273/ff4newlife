/**
 * @file ff4_state.c
 * @brief Native game state management (Phase 1 core).
 *
 * Pure C99, no hardware access. All data comes in through the caller;
 * Phase 2 (asset_loader) will populate character start data (CharProp /
 * CharInitEquip) and the level curve (LevelUpProp) from ff4-en-data.json.
 */
#include <stdint.h>
#include <string.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static void member_reset(FF4_PartyMember *m)
{
    memset(m, 0, sizeof *m);
    m->id = FF4_CHAR_NONE;
    m->level = 1;
    m->speed_mod = FF4_ATB_NORMAL;
    /* Provisional neutral defaults; real values come from CharProp JSON. */
    m->max_hp = 1;
    m->hp = 1;
}

static void member_clear(FF4_PartyMember *m)
{
    member_reset(m);
}

/**
 * Provisional level curve: cumulative EXP required before a level-up.
 *
 * exp_to_next(level) ramps linearly from 100 at level 1. This is a
 * stand-in ONLY: Phase 2 wires the per-character LevelUpProp arrays from
 * ff4-en-data.json and Phase 3 recalculates stats on level-up. No game
 * balance code spends time here.
 */
static uint32_t exp_to_next(uint8_t level)
{
    return 100u + 90u * (uint32_t)(level > 0 ? level - 1 : 0);
}

/* ------------------------------------------------------------------ */
/*  Game state lifecycle                                               */
/* ------------------------------------------------------------------ */

void ff4_state_init(FF4_GameState *g)
{
    int i;

    memset(g, 0, sizeof *g);
    for (i = 0; i < FF4_PARTY_MAX; ++i)
        member_reset(&g->party[i]);

    /* Defaults matching a fresh boot of the original (config $16A3+). */
    g->settings.battle_mode = FF4_BATTLE_MODE_WAIT;
    g->settings.row_setting = FF4_ROWSET_3FRONT_2BACK;
    g->settings.text_speed = FF4_TEXT_FASTEST + 1;
    g->settings.battle_speed = 3;
    g->settings.controller_mode = FF4_CTRL_SINGLE;
    g->settings.mono_audio = false;
    g->settings.window_color[0] = 28;
    g->settings.window_color[1] = 28;
    g->settings.window_color[2] = 28;

    ff4_state_reset_world(&g->world);
    ff4_battle_init(&g->battle);
}

void ff4_state_reset_world(FF4_WorldState *w)
{
    memset(w, 0, sizeof *w);
    w->map_type = FF4_MAP_SUB;
    w->world = FF4_WORLD_OVERWORLD;
    w->map_id = 0x00;            /* Town of Baron (new-game start). */
    w->facing = FF4_DIR_DOWN;
    w->battle_enabled = true;
    w->player_control = true;
}

void ff4_party_add(FF4_GameState *g, FF4_CharacterId id, uint8_t slot)
{
    if (slot >= FF4_PARTY_MAX || id == FF4_CHAR_NONE)
        return;
    member_clear(&g->party[slot]);
    g->party[slot].id = id;
    if (slot == 0) {
        /* Party leader drives the field sprite; keep parallel. */
        g->world.showing_character = 0;
    }
}

void ff4_party_remove(FF4_GameState *g, uint8_t slot)
{
    if (slot >= FF4_PARTY_MAX)
        return;
    member_clear(&g->party[slot]);
}

FF4_PartyMember *ff4_party_member(FF4_GameState *g, uint8_t slot)
{
    if (slot >= FF4_PARTY_MAX)
        return NULL;
    return &g->party[slot];
}

/* ------------------------------------------------------------------ */
/*  Status flags                                                       */
/* ------------------------------------------------------------------ */

bool ff4_status_has(const uint8_t status[4], uint8_t byte, uint8_t mask)
{
    if (byte >= 4)
        return false;
    return (status[byte] & mask) != 0;
}

void ff4_status_set(uint8_t status[4], uint8_t byte, uint8_t mask)
{
    if (byte < 4)
        status[byte] |= mask;
}

void ff4_status_clear(uint8_t status[4], uint8_t byte, uint8_t mask)
{
    if (byte < 4)
        status[byte] &= (uint8_t)~mask;
}

/* ------------------------------------------------------------------ */
/*  Inventory                                                          */
/* ------------------------------------------------------------------ */

int ff4_inventory_find(const FF4_Inventory *inv, FF4_ItemId id)
{
    int i;

    for (i = 0; i < FF4_INVENTORY_SLOTS; ++i)
        if (inv->slots[i].item_id == (uint8_t)id && inv->slots[i].quantity > 0)
            return i;
    return -1;
}

bool ff4_inventory_has_item(const FF4_Inventory *inv, FF4_ItemId id)
{
    return ff4_inventory_find(inv, id) >= 0;
}

uint8_t ff4_inventory_count(const FF4_Inventory *inv)
{
    uint8_t n = 0;
    int i;

    for (i = 0; i < FF4_INVENTORY_SLOTS; ++i)
        if (inv->slots[i].quantity > 0)
            ++n;
    return n;
}

bool ff4_inventory_add(FF4_Inventory *inv, FF4_ItemId id, uint8_t qty)
{
    int slot;
    int i;

    if (id == FF4_ITEM_NONE || qty == 0)
        return true;

    /* Stack onto an existing pile first (cap FF4_ITEM_QTY_MAX). */
    slot = ff4_inventory_find(inv, id);
    if (slot >= 0) {
        inv->slots[slot].quantity =
            (uint8_t)((inv->slots[slot].quantity + qty > FF4_ITEM_QTY_MAX)
                          ? FF4_ITEM_QTY_MAX
                          : inv->slots[slot].quantity + qty);
        return true;
    }

    /* Else find an empty slot. */
    for (i = 0; i < FF4_INVENTORY_SLOTS; ++i) {
        if (inv->slots[i].quantity == 0) {
            inv->slots[i].item_id = (uint8_t)id;
            inv->slots[i].quantity = qty;
            inv->count = ff4_inventory_count(inv);
            return true;
        }
    }
    return false;                /* Bag full ($181D pointer would be $FF.) */
}

bool ff4_inventory_remove(FF4_Inventory *inv, FF4_ItemId id, uint8_t qty)
{
    int slot = ff4_inventory_find(inv, id);

    if (slot < 0)
        return false;
    if (inv->slots[slot].quantity <= qty) {
        inv->slots[slot].item_id = FF4_ITEM_NONE;
        inv->slots[slot].quantity = 0;
    } else {
        inv->slots[slot].quantity -= qty;
    }
    inv->count = ff4_inventory_count(inv);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Battle                                                             */
/* ------------------------------------------------------------------ */

void ff4_battle_init(FF4_BattleState *b)
{
    int i;

    memset(b, 0, sizeof *b);
    b->atb_timer_type = FF4_TIMER_ATB;
    b->can_escape = true;
    for (i = 0; i < FF4_COMBATANT_SLOTS_MAX; ++i)
        b->present[i] = false;
}

/**
 * Advance the ATB clock: every tick adds 1 to each active, alive combatant's
 * action timer. When the timer reaches the combatant's base duration the
 * pending flag is raised and the battle scheduler (Phase 5) will pick the
 * next actor. Mirrors timer semantics of $29EA/$2A04 (m: action enabled,
 * p: timer expired/action pending).
 */
void ff4_battle_tick(FF4_BattleState *b, uint16_t frames)
{
    int i;
    uint16_t dur;

    if (!b->active)
        return;

    for (i = 0; i < FF4_COMBATANT_SLOTS_MAX; ++i) {
        if (!b->present[i])
            continue;

        if (i < FF4_PARTY_MAX) {
            const FF4_PartyMember *m = &b->party[i];
            if (m->status[0] & FF4_ST1_DEAD)
                continue;        /* Dead combatants never fill the gauge. */
            dur = m->base_timer > 0 ? m->base_timer : m->speed_mod;
        } else {
            const FF4_Monster *mo = &b->monsters[i - FF4_PARTY_MAX];
            if (mo->core.status[0] & FF4_ST1_DEAD)
                continue;
            dur = mo->base_timer_dur > 0 ? mo->base_timer_dur
                                         : mo->core.speed_mod;
        }

        b->timers[i].value += frames;
        if (b->timers[i].value >= dur) {
            b->timers[i].value = dur;
            b->timers[i].flags |= 0x01; /* p: expired / action pending */
            b->timer_enabled &= (uint16_t)~(1u << i);
        }
    }
}

void ff4_battle_queue_action(FF4_BattleState *b, const FF4_PendingAction *a)
{
    if (b->queue_size >= 5)
        return;                  /* Scheduler (Phase 5) drains per round. */
    b->queue[b->queue_size++] = *a;
}

/* ------------------------------------------------------------------ */
/*  Experience                                                         */
/* ------------------------------------------------------------------ */

uint8_t ff4_exp_apply(FF4_PartyMember *m, uint32_t exp_gain)
{
    uint8_t levels = 0;
    uint32_t need;

    m->exp += exp_gain;
    if (m->level >= FF4_LEVEL_MAX)
        return 0;

    /* Level up until the remaining EXP cannot cross the next threshold.
       Stat/HP/MP growth hooks into LevelUpProp in Phase 2/3; this only
       tracks level progression so the game loop can react. */
    while (m->level < FF4_LEVEL_MAX) {
        need = exp_to_next(m->level);
        if (m->exp < need)
            break;
        m->exp -= need;
        ++m->level;
        ++levels;
    }
    return levels;
}