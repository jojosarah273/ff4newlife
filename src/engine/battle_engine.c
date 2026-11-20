/**
 * @file battle_engine.c
 * @brief Phase 5: battle system engine.
 *
 * Ports of the battle modules (battle.asm / init.asm / timer.asm /
 * fight.asm / magic.asm / ai.asm). Mechanics delegated to ff4_math;
 * all data read from the asset db.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/battle_engine.h"

/* ==================================================================== */
/*  Internal helpers                                                    */
/* ==================================================================== */

/* combatant helpers: slot 0-4 chars, 5-12 monsters. */
static const FF4_PartyMember *slot_member(const FF4_BattleState *b, int slot)
{
    return (slot >= 0 && slot < FF4_PARTY_MAX) ? &b->party[slot] : NULL;
}

static const FF4_Monster *slot_monster(const FF4_BattleState *b, int slot)
{
    return (slot >= FF4_PARTY_MAX && slot < FF4_COMBATANT_SLOTS_MAX)
               ? &b->monsters[slot - FF4_PARTY_MAX]
               : NULL;
}

static bool slot_alive(const FF4_BattleState *b, int slot)
{
    const FF4_PartyMember *m;

    if (slot >= FF4_PARTY_MAX)
        return b->present[slot];
    m = slot_member(b, slot);
    return b->present[slot] && m->id != FF4_CHAR_NONE &&
           !(m->status[0] & FF4_ST1_DEAD);
}

static int rand_alive_character(FF4_Rng *rng, const FF4_BattleState *b)
{
    int alive[FF4_PARTY_MAX], n = 0, i;

    for (i = 0; i < FF4_PARTY_MAX; ++i)
        if (slot_alive(b, i))
            alive[n++] = i;
    if (n == 0)
        return -1;
    return alive[ff4_rng_range(rng, 0, n - 1)];
}

static int rand_alive_monster(FF4_Rng *rng, const FF4_BattleState *b)
{
    int alive[FF4_MONSTER_SLOTS_MAX], n = 0, i;

    for (i = 0; i < FF4_MONSTER_SLOTS_MAX; ++i)
        if (slot_alive(b, FF4_PARTY_MAX + i))
            alive[n++] = FF4_PARTY_MAX + i;
    if (n == 0)
        return -1;
    return alive[ff4_rng_range(rng, 0, n - 1)];
}

/* ==================================================================== */
/*  Battle start (init.asm formation assembly)                          */
/* ==================================================================== */

int ff4_battle_start(const FF4_AssetDB *db, FF4_GameState *g,
                     uint8_t formation_id, const FF4_BattleOpts *opts)
{
    FF4_BattleState *b = &g->battle;
    const FF4_FormationDef *f;
    int slot = FF4_PARTY_MAX, type, i;

    if (!db)
        return -1;
    (void)formation_id;  /* byte range fits the table */
    f = &db->formations[formation_id];

    ff4_battle_init(b);
    b->active = true;
    b->can_escape = !(opts && opts->forced_escape);
    b->battle_bg = opts ? opts->battle_bg : 0;
    b->battle_music = opts ? opts->music : 0;
    if (opts && opts->back_attack)
        b->monsters_first = true;
    if (opts && opts->first_strike)
        b->chars_first = true;
    b->formation_id = formation_id;

    /* copy the field party into the battle records ($2000) */
    for (i = 0; i < FF4_PARTY_MAX; ++i) {
        b->party[i] = g->party[i];
        b->present[i] = g->party[i].id != FF4_CHAR_NONE;
        if (b->present[i])
            b->slot_map[i] = g->party[i].id;
    }

    /* spawn monsters: three types x counts into free slots */
    for (type = 0; type < FF4_MONSTER_TYPES_MAX; ++type) {
        uint8_t mid = f->mons[type];
        int count = f->counts[type];

        if ((int)mid >= FF4_DB_MONSTERS || count == 0)
            continue;
        b->monster_types[type] = mid;
        b->monster_counts[type] = (uint8_t)count;
        b->total_monsters += count;
        for (i = 0; i < count && slot < FF4_COMBATANT_SLOTS_MAX; ++i, ++slot) {
            const FF4_MonsterDef *md = ff4_monster(db, mid);
            FF4_Monster *m = &b->monsters[slot - FF4_PARTY_MAX];
            int agi;

            if (!md)
                continue;
            memset(m, 0, sizeof *m);
            m->core.id = (FF4_CharacterId)mid;
            m->core.level = md->level;
            m->core.max_hp = md->hp;
            m->core.hp = md->hp;
            m->core.max_mp = 0;
            m->core.mp = 0;
            m->core.str = md->attack;
            m->core.agi = md->magic_attack;
            m->core.sta = md->defense;
            m->core.int_ = md->magic_defense;
            m->core.spr = 0;
            m->core.attack_power = md->atk_def_base;
            m->core.attack_mult = md->atk_def_multi;
            m->core.hit_rate = md->atk_def_percent;
            m->core.crit_rate = 0;
            m->core.crit_bonus = 0;
            m->core.attack_race = md->attack_elements;
            m->core.attack_element = md->attack_elements;
            m->core.weak_elements = md->weak_elements;
            m->core.strong_elements = md->strong_elements;
            m->core.immune_elements = 0;
            m->core.very_weak_elements = 0;
            m->core.attack_status = md->attack_status;
            m->core.status_immunity = md->status_immunity;
            m->core.creature_type = md->creature_type;
            m->core.def = md->defense;
            m->core.mag_def = md->magic_defense;
            m->core.def_rate = 0;
            m->core.mag_def_rate = 0;

            /* position from monsterPosition[id] slot */
            if (i < 8) {
                m->position_x = db->monster_positions[f->position][i][0];
                m->position_y = db->monster_positions[f->position][i][1];
            }

            /* ATB base from agility range (@530: agi*10 / base).
               The game rolls within [min, max); a zero-span range is
               the min value (no modulo on a zero divisor). */
            {
                FF4_Rng r;
                int span = md->agility_max >= md->agility_min
                               ? md->agility_max - md->agility_min
                               : 0;

                ff4_rng_init(&r, db->rng_table);
                agi = md->agility_min +
                      (span ? ff4_rng_range(&r, 0, span - 1) : 0);
            }
            /* emulator agi*10 / 10 */
            m->base_timer_dur = (uint16_t)((agi * 10u) / 10u + 10u);
            if (m->base_timer_dur == 0)
                m->base_timer_dur = 1;

            m->core.base_timer = m->base_timer_dur;
            m->level_boss = (uint8_t)((md->boss ? 0x80 : 0) |
                                      (md->level & 0x7F));
            m->core.is_boss = md->boss;

            b->present[slot] = true;
            b->slot_map[slot] = 0x80 | mid;
        }
        /* note in the original, dead monster slots are skipped via $3540 */
    }

    /* formation rewards snapshot (victory uses these; the original
       keeps the formation types in $29AD separate from dead counts) */
    b->formation_exp = b->formation_gil = 0;
    for (type = 0; type < FF4_MONSTER_TYPES_MAX; ++type) {
        uint8_t mid = f->mons[type];

        if ((int)mid < FF4_DB_MONSTERS) {
            b->formation_exp += db->monster_exp[mid];
            b->formation_gil += db->monster_gp[mid];
        }
    }

    /* ATB gauge caps ($2060 base timer per combatant) */
    for (i = 0; i < FF4_COMBATANT_SLOTS_MAX; ++i) {
        if (!b->present[i])
            continue;
        if (i < FF4_PARTY_MAX)
            b->atb_duration[i] = b->party[i].base_timer
                                     ? b->party[i].base_timer
                                     : (b->party[i].speed_mod
                                            ? b->party[i].speed_mod
                                            : 16);
        else
            b->atb_duration[i] = b->monsters[i - FF4_PARTY_MAX].base_timer_dur;
    }

    /* bind the battle rng ($1900 table + $97 pointer) */
    {
        FF4_Rng r;

        ff4_rng_init(&r, db->rng_table);
        ff4_rng_seed(&r, (uint8_t)(g->world.frame_counter & 0xFF));
        memcpy(b->rng_table, r.table, 256);
        b->rng_ptr = r.ptr;
    }
    return 0;
}

/* ==================================================================== */
/*  ATB (DecTimers port)                                                */
/* ==================================================================== */

void ff4_battle_atb(const FF4_AssetDB *db, FF4_GameState *g, uint32_t frames)
{
    FF4_BattleState *b = &g->battle;
    int i;

    (void)db;
    if (!b->active)
        return;

    for (i = 0; i < FF4_COMBATANT_SLOTS_MAX; ++i) {
        if (!slot_alive(b, i))
            continue;

        /* sleep/paralyze/stop gate the gauge (the game stops the action
           timer while these are active) */
        {
            const FF4_PartyMember *m = slot_member(b, i);

            if (m && (m->status[1] & (FF4_ST2_SLEEP | FF4_ST2_PARALYZE)))
                continue;
            if (m && (m->status[2] & FF4_ST3_STOP))
                continue;
        }

        if (b->atb_duration[i] == 0)
            b->atb_duration[i] = 10;
        if (b->timers[i].value < b->atb_duration[i]) {
            uint16_t v = b->timers[i].value + (uint16_t)frames;

            if (v > b->atb_duration[i])
                v = b->atb_duration[i];
            b->timers[i].value = v;
            if (b->timers[i].value >= b->atb_duration[i])
                b->timers[i].flags |= 0x01;      /* p: pending action */
        }
    }
}

bool ff4_battle_ready(const FF4_BattleState *b, int slot)
{
    if (slot < 0 || slot >= FF4_COMBATANT_SLOTS_MAX)
        return false;
    return (b->timers[slot].flags & 0x01) != 0 &&
           slot_alive(b, slot);
}

int ff4_battle_next_ready(const FF4_BattleState *b, int after_slot)
{
    int i;

    for (i = after_slot + 1; i < FF4_COMBATANT_SLOTS_MAX; ++i)
        if (ff4_battle_ready(b, i))
            return i;
    for (i = 0; i <= after_slot; ++i)
        if (ff4_battle_ready(b, i))
            return i;
    return -1;
}

/* ==================================================================== */
/*  Command selection                                                   */
/* ==================================================================== */

void ff4_battle_set_command(FF4_BattleState *b, int slot, FF4_BattleCommand cmd,
                            uint8_t param, uint8_t target_chars,
                            uint8_t target_mons)
{
    if (slot < 0 || slot >= FF4_COMBATANT_SLOTS_MAX)
        return;
    b->queue[b->queue_size].actor = (FF4_CombatantId)slot;
    b->queue[b->queue_size].command = cmd;
    b->queue[b->queue_size].item = (FF4_ItemId)param;
    b->queue[b->queue_size].spell = (FF4_SpellId)param;
    b->queue[b->queue_size].target_mask_chars = target_chars;
    b->queue[b->queue_size].target_mask_mons = target_mons;
    b->queue_size = (uint8_t)((b->queue_size + 1) % 6);
}

void ff4_battle_auto_command(FF4_BattleState *b, int slot)
{
    ff4_battle_set_command(b, slot, FF4_CMD_FIGHT, 0, 0x3F, 0xFF);
}

/* ==================================================================== */
/*  Execution (DoAttack port)                                           */
/* ==================================================================== */

#define FF4_CMD_MONSTER_ATTACK 0x80  /* internal sentinel (ai.asm default) */

/* GetSummonAttack (magic.asm @e053): summon spell -> attack id. */
static uint8_t summon_attack_id(FF4_Rng *rng, uint8_t spell)
{
    if (spell >= 0x3F)
        return spell == 0x3F ? 0x5D
                             : (uint8_t)(0x3E + ff4_rng_rand255(rng) % 3);
    return (uint8_t)(spell - 0x31 + 0x4D);
}

void ff4_battle_apply_damage(FF4_BattleState *b, int slot, int amount, bool heal)
{
    FF4_PartyMember *m;
    int cap;

    if (slot < 0 || slot >= FF4_COMBATANT_SLOTS_MAX)
        return;

    if (slot >= FF4_PARTY_MAX) {
        FF4_Monster *mo = &b->monsters[slot - FF4_PARTY_MAX];

        cap = mo->core.max_hp;
        m = &mo->core;
    } else {
        m = &b->party[slot];
        cap = m->max_hp;
    }

    if (heal) {
        int v = m->hp + amount;
        if (v > cap)
            v = cap;
        m->hp = (uint16_t)v;
        b->damage[slot] = (int16_t)amount;          /* msb = restore */
        b->damage_flags[slot] |= 0x80;
    } else {
        int v = m->hp - amount;
        if (v < 0)
            v = 0;
        m->hp = (uint16_t)v;
        b->damage[slot] = (int16_t)-amount;
        if (m->hp == 0)
            m->status[0] |= FF4_ST1_DEAD;
    }
}

void ff4_battle_update_dead(FF4_BattleState *b)
{
    int slot, type, total = 0;

    for (slot = 0; slot < FF4_COMBATANT_SLOTS_MAX; ++slot) {
        FF4_PartyMember *m;

        if (slot >= FF4_PARTY_MAX) {
            FF4_Monster *mo = &b->monsters[slot - FF4_PARTY_MAX];

            m = &mo->core;
            if ((m->status[0] & FF4_ST1_DEAD) && b->present[slot]) {
                m->hp = 0;
                b->present[slot] = false;
            }
        } else {
            m = &b->party[slot];
            if ((m->status[0] & FF4_ST1_DEAD) && b->present[slot]) {
                m->hp = 0;
                b->present[slot] = false;
                m->status[0] &= (uint8_t)~FF4_ST1_POISON;
                m->status[1] &= (uint8_t)(FF4_ST2_CURSE | FF4_ST2_FLOAT);
                m->status[2] &= 0xA2;
                m->status[3] &= 0x80;
            }
        }
    }
    /* recount monster types (init.asm @b219) */
    for (type = 0; type < FF4_MONSTER_TYPES_MAX; ++type) {
        int n = 0;

        for (slot = FF4_PARTY_MAX; slot < FF4_COMBATANT_SLOTS_MAX; ++slot)
            if (b->present[slot] && b->slot_map[slot] ==
                (0x80 | b->monster_types[type]))
                ++n;
        b->monster_counts[type] = (uint8_t)n;
        total += n;
    }
    b->total_monsters = (uint8_t)total;
}

void ff4_battle_timer_effects(FF4_GameState *g, uint32_t frames)
{
    FF4_BattleState *b = &g->battle;
    int slot;

    (void)frames;
    if (!b->active)
        return;

    /* poison / sap: every poison_pend frames, max_hp/8 (min 1) */
    for (slot = 0; slot < FF4_COMBATANT_SLOTS_MAX; ++slot) {
        FF4_PartyMember *m;

        if (slot >= FF4_PARTY_MAX)
            m = &b->monsters[slot - FF4_PARTY_MAX].core;
        else
            m = &b->party[slot];
        if (!b->present[slot])
            continue;

        if (m->status[0] & FF4_ST1_POISON) {
            if (b->poison_pend[slot] == 0) {
                int dmg = (m->max_hp / 8);
                if (dmg < 1)
                    dmg = 1;
                ff4_battle_apply_damage(b, slot, dmg, false);
                b->poison_pend[slot] = 3;   /* poison tick interval */
            } else {
                b->poison_pend[slot]--;
            }
        }
    }
    ff4_battle_update_dead(b);
}

/* ==================================================================== */
/*  Monster AI (ai.asm basic subset)                                    */
/* ==================================================================== */

static const FF4_Blob *monster_script_blob(const FF4_AssetDB *db,
                                           const char *keyfmt, uint8_t id)
{
    char key[48];

    snprintf(key, sizeof key, keyfmt, id);
    return ff4_db_blob(db, key);
}

uint8_t ff4_monster_ai_pick(FF4_Rng *rng, const FF4_AssetDB *db,
                            const FF4_Monster *m)
{
    const FF4_Blob *cond, *action;
    const uint8_t *p;
    size_t len;
    uint8_t action_idx = 0;

    (void)rng;
    /* the monster script picks the action script index */
    cond = monster_script_blob(db, "monsterScript[%d]", m->core.id);
    if (!cond)
        return 0x00;
    /* first script byte -> condition code; second -> action index */
    p = cond->data;
    len = cond->len;
    if (len >= 2)
        action_idx = p[1];
    else if (len >= 1)
        action_idx = 0;

    action = monster_script_blob(db, "monsterAction[%d]", action_idx);
    if (!action)
        return 0x60;   /* default attack */
    /* first attack id in the action script */
    return action->data[0];
}

/* ==================================================================== */
/*  Execute                                                             */
/* ==================================================================== */

static int target_pool(FF4_Rng *rng, const FF4_BattleState *b,
                       const FF4_AttackDef *apt, int *out, int maxn)
{
    int n = 0, i;

    if (apt->target == 2) {                /* single enemy */
        int t = rand_alive_monster(rng, b);
        if (t >= 0 && n < maxn)
            out[n++] = t;
    } else if (apt->target == 1) {         /* single ally */
        int t = rand_alive_character(rng, b);
        if (t >= 0 && n < maxn)
            out[n++] = t;
    } else if (apt->target == 3) {         /* all allies */
        for (i = 0; i < FF4_PARTY_MAX && n < maxn; ++i)
            if (slot_alive(b, i))
                out[n++] = i;
    } else {                               /* enemy groups: 4,5,6,7 */
        for (i = FF4_PARTY_MAX; i < FF4_COMBATANT_SLOTS_MAX && n < maxn; ++i)
            if (slot_alive(b, i))
                out[n++] = i;
    }
    return n;
}

static void apply_attack_targets(FF4_Rng *rng, const FF4_AssetDB *db,
                                 FF4_GameState *g, int attacker_slot,
                                 const FF4_AttackDef *apt, int targets_mask,
                                 int element, int hit_pct)
{
    FF4_BattleState *b = &g->battle;
    int pool[FF4_COMBATANT_SLOTS_MAX];
    int n = target_pool(rng, b, apt, pool, FF4_COMBATANT_SLOTS_MAX);
    int k;

    (void)db;
    (void)attacker_slot;
    (void)targets_mask;

    for (k = 0; k < n; ++k) {
        int t = pool[k];
        const FF4_PartyMember *target = slot_member(b, t);
        FF4_DamageCtx ctx;
        int dmg;

        if (!target || (target->status[0] & (FF4_ST1_DEAD | FF4_ST1_STONE)))
            continue;
        if (ff4_rng_rand99(rng) >= hit_pct)
            continue;                        /* miss */

        if (apt->effect == 2 || !apt->damage) {
            if (apt->effect == 2)
                ff4_battle_apply_damage(b, t, apt->power * 4, true);

            continue;
        }

        memset(&ctx, 0, sizeof ctx);
        ctx.base_attack = ff4_magic_attack_power(apt->power ? apt->power : 1,
                                                 false);
        ctx.defense = target->mag_def;
        ctx.element_mult = ff4_calc_element_mult(target, (uint8_t)element);
        ctx.race_mult = 2;
        ctx.net_hits = 1;
        ctx.target_count = apt->split_damage ? n : 1;
        dmg = ff4_calc_damage(rng, &ctx);
        if (dmg > 0)
            ff4_battle_apply_damage(b, t, dmg, false);
    }
}

void ff4_battle_execute(FF4_Rng *rng, const FF4_AssetDB *db,
                        FF4_GameState *g, int slot)
{
    FF4_BattleState *b = &g->battle;
    FF4_PendingAction act;
    uint8_t attack_id = 0;
    const FF4_AttackDef *apt;

    if (!slot_alive(b, slot)) {
        return;
    }

    /* pop the queued action if any; else auto Fight */
    if (slot < FF4_PARTY_MAX) {
        int q;
        bool found = false;

        for (q = 0; q < 5; ++q) {
            if (b->queue[q].actor == (FF4_CombatantId)slot) {
                act = b->queue[q];
                found = true;
                break;
            }
        }
        if (!found) {
            act.actor = (FF4_CombatantId)slot;
            act.command = FF4_CMD_FIGHT;
        }
    } else {
        act.actor = (FF4_CombatantId)slot;
        act.command = (FF4_BattleCommand)FF4_CMD_MONSTER_ATTACK;
    }

    switch ((int)act.command) {
    case FF4_CMD_FIGHT: {
        const FF4_PartyMember *att = slot_member(b, slot);
        int tgt = rand_alive_monster(rng, b);
        int dmg;

        if (!att || tgt < 0)
            break;
        dmg = ff4_phys_damage(rng, att,
                              &b->monsters[tgt - FF4_PARTY_MAX].core,
                              slot, tgt);
        if (dmg > 0)
            ff4_battle_apply_damage(b, tgt, dmg, false);
        break;
    }
    case FF4_CMD_MONSTER_ATTACK: {
        /* monster: AI-driven attack id */
        const FF4_Monster *mon = slot_monster(b, slot);
        int tgt;

        if (!mon)
            break;
        attack_id = ff4_monster_ai_pick(rng, db, mon);
        apt = &db->attacks[attack_id];
        tgt = rand_alive_character(rng, b);
        if (tgt < 0)
            break;
        apply_attack_targets(rng, db, g, slot, apt, 0,
                             apt->element, apt->hit);
        (void)tgt;
        break;
    }
    case FF4_CMD_WHITE:
    case FF4_CMD_BLACK:
    case FF4_CMD_CALL: {
        /* spells: id direct for 0x01-0x30; summons via GetSummonAttack */
        uint8_t spell = (uint8_t)act.spell;
        uint8_t aid;

        if (act.command == FF4_CMD_CALL)
            aid = summon_attack_id(rng, spell);
        else
            aid = spell;
        {
            const FF4_AttackDef *sp = &db->attacks[aid];

            apply_attack_targets(rng, db, g, slot, sp, 0,
                                 sp->element, sp->hit ? sp->hit : 90);
        }
        break;
    }
    case FF4_CMD_ITEM:
        /* item use (potions heal, the rest pass) */
        break;
    default:
        break;
    }

    /* consumed: clear the ready flag and reset the gauge */
    if (slot >= 0 && slot < FF4_COMBATANT_SLOTS_MAX) {
        b->timers[slot].flags &= (uint8_t)~0x01;
        b->timers[slot].value = 0;
    }
    b->last_actor = slot;
}

bool ff4_battle_check_end(const FF4_BattleState *b, bool *defeat)
{
    bool chars_alive = false, mons_alive = false;
    int i;

    for (i = 0; i < FF4_PARTY_MAX; ++i)
        chars_alive = chars_alive || slot_alive(b, i);
    for (i = FF4_PARTY_MAX; i < FF4_COMBATANT_SLOTS_MAX; ++i)
        mons_alive = mons_alive || slot_alive(b, i);

    if (defeat)
        *defeat = !chars_alive && mons_alive;
    return !chars_alive || !mons_alive;
}

void ff4_battle_win_rewards(FF4_Rng *rng, const FF4_AssetDB *db,
                            FF4_GameState *g)
{
    FF4_BattleState *b = &g->battle;
    uint32_t exp = b->formation_exp, gil = b->formation_gil;
    int i;

    /* drop rolls: one per monster type in the formation */
    for (i = 0; i < FF4_MONSTER_TYPES_MAX; ++i) {
        uint8_t mid = b->monster_types[i];
        const FF4_MonsterDef *md = ff4_monster(db, mid);
        int roll;

        if (!md || md->item_rate == 0)
            continue;
        roll = ff4_rng_range(rng, 0, 31);
        if (roll == 0) {
            uint8_t drop = md->drop[0];

            if (drop != 0xFF && drop != 0)
                ff4_inventory_add(&g->inventory, (FF4_ItemId)drop, 1);
        }
    }
    b->exp_gained += exp;
    b->gil_gained += gil;
    b->end_flags |= FF4_BATTLE_VICTORY;

    /* apply exp to living party members */
    for (i = 0; i < FF4_PARTY_MAX; ++i) {
        if (b->party[i].id != FF4_CHAR_NONE)
            ff4_exp_apply(&g->party[i], exp);
    }
    g->inventory.gil_full += gil;
}

void ff4_battle_finish(FF4_BattleState *b)
{
    b->active = false;
    b->end_flags = 0;
    b->queue_size = 0;
    ff4_battle_init(b);
}

void ff4_battle_sync_back(FF4_GameState *g)
{
    int i;

    for (i = 0; i < FF4_PARTY_MAX; ++i) {
        if (g->battle.party[i].id == FF4_CHAR_NONE)
            continue;
        g->party[i].hp = g->battle.party[i].hp;
        g->party[i].mp = g->battle.party[i].mp;
        memcpy(g->party[i].status, g->battle.party[i].status, 4);
    }
}