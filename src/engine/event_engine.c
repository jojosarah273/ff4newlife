/**
 * @file event_engine.c
 * @brief Phase 4: event script interpreter (port of field/event.asm).
 */
#include <string.h>

#include "ff4/engine/event_engine.h"

/* ==================================================================== */
/*  Command decoding (the copier table, event.asm @e206-@e28f)          */
/* ==================================================================== */

/* parameter byte count per command (see header for the derivation). */
static const uint8_t cmd_params[0x100] = {
    /* 0x00-0xCF: actions, 1 byte each (table indexed by opcode) */
    [0x00 ... 0xCF] = 0,
    /* 0xD0-0xDA: no parameters */
    [0xD0 ... 0xDA] = 0,
    /* 0xDB-0xE1: 1 parameter */
    [0xDB ... 0xE1] = 1,
    /* 0xE2: 2 parameters (spell list + spell) */
    [0xE2] = 2,
    /* 0xE3-0xFD: 1 parameter */
    [0xE3 ... 0xFD] = 1,
    /* 0xFE: 3 parameters (map, packed dir+x, y) */
    [0xFE] = 3,
    /* 0xFF: end */
    [0xFF] = 0
};

int ff4_event_decode(const uint8_t *buf, size_t len, size_t *pos,
                     FF4_EventCmdDecoded *out)
{
    size_t p = *pos;
    uint8_t op;
    int n, i;

    if (p >= len)
        return -1;

    op = buf[p++];
    memset(out, 0, sizeof *out);
    out->id = (FF4_EventCmd)op;

    if (op < 0xD0) {
        out->obj = (op >> 4) & 0x0F;      /* 0xF0 mask: object/npc index */
        out->act = op & 0x0F;             /* 0x0F mask: action */
        out->next = p;
        *pos = p;
        return 0;
    }

    if (op == 0xFF) {
        out->next = p;
        *pos = p;
        return 0;
    }

    if (op == 0xEB) {
        /* repeat: count + length, then the body which the caller skips. */
        if (p + 2 > len)
            return -1;
        out->args[0] = buf[p];       /* repeat count */
        out->args[1] = buf[p + 1];   /* body length */
        out->arg_count = 2;
        p += 2 + buf[p + 1];
        out->next = p;
        *pos = p;
        return 0;
    }

    n = cmd_params[op];
    for (i = 0; i < n && p < len; ++i)
        out->args[i] = buf[p++];
    if (i < n)
        return -1;
    out->arg_count = n;
    out->next = p;
    *pos = p;
    return 0;
}

/* ==================================================================== */
/*  Party save/restore slot tables (event.asm @e922/@e9a5)              */
/* ==================================================================== */

/* characters to add (restore saved character if msb set); low bits =
   index into the five saved character slots ($1140 + slot * 0x40). */
const uint8_t ff4_char_remove_tbl[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x81, 0x09,
    0x81, 0x83, 0x0A, 0x80, 0x82, 0x84, 0x0B, 0x0C, 0x80, 0x0D
};

/* characters to remove (don't save if msb set); low bits = save slot. */
const uint8_t ff4_char_add_tbl[21] = {
    0x80, 0x00, 0x04, 0x01, 0x80, 0x02, 0x03, 0x80, 0x80, 0x01, 0x80,
    0x80, 0x80, 0x01, 0x00, 0x80, 0x80, 0x80, 0x01, 0x80
};

/* ==================================================================== */
/*  Dialogue text                                                       */
/* ==================================================================== */

const char *ff4_event_dialog_text(const FF4_AssetDB *db, int bank, int id)
{
    if (!db || bank < 0 || bank > 2)
        return "";
    if (bank == 0)
        return (id >= 0 && id < 384) ? db->map_dialog[id] : "";
    if (bank == 1)
        return (id >= 0 && id < 512) ? db->event_dialog1[id] : "";
    return (id >= 0 && id < 512) ? db->event_dialog2[id] : "";
}

/* ==================================================================== */
/*  State helpers                                                       */
/* ==================================================================== */

static void set_switch(uint8_t *flags, size_t nbytes, uint8_t id, bool on)
{
    uint8_t byte = id >> 3, bit = 1u << (id & 7);

    if (byte < nbytes) {
        if (on)
            flags[byte] |= bit;
        else
            flags[byte] &= (uint8_t)~bit;
    }
}

/* find a party slot holding the given character id (or -1). */
static int find_member(const FF4_GameState *g, uint8_t char_id)
{
    int i;

    for (i = 0; i < FF4_PARTY_MAX; ++i)
        if (g->party[i].id == char_id)
            return i;
    return -1;
}

/* load a character from CharProp + CharInitEquip (event.asm LoadCharProp
   + InitCharEquip, incl. the adult-Rydia class swap). */
static void load_char_prop(const FF4_AssetDB *db, FF4_PartyMember *m,
                           uint8_t char_id)
{
    const FF4_CharDef *c = ff4_char(db, char_id);

    memset(m, 0, sizeof *m);
    m->id = char_id;
    if (!c)
        return;

    m->job = c->job;
    m->right_handed = c->right_handed;
    m->left_handed = c->left_handed;
    m->level = c->level;
    m->exp = c->exp;
    m->exp_last_level = c->exp_last_level;
    m->max_hp = c->hp;
    m->hp = c->cur_hp;
    m->max_mp = c->mp;
    m->mp = c->cur_mp;
    m->base_str = c->str;
    m->base_agi = c->agi;
    m->base_sta = c->sta;
    m->base_int = c->intel;
    m->base_spr = c->spr;
    m->crit_rate = c->crit_pct;
    m->crit_bonus = c->crit_bonus;
    m->speed_mod = c->speed_mod;

    /* @e848: adult Rydia switches to the adult summoner class */
    m->job = (char_id == FF4_CHAR_RYDIA_ADULT)
                 ? FF4_JOB_CALLER_ADULT
                 : c->job;

    /* CharInitEquip row = char id - 1 (event.asm _e8f6: (id-1)*7) */
    m->equip[FF4_EQ_RIGHT_HAND] = c->eq_right;
    m->right_qty = c->eq_right_qty;
    m->equip[FF4_EQ_LEFT_HAND] = c->eq_left;
    m->left_qty = c->eq_left_qty;
    m->equip[FF4_EQ_HEAD] = c->eq_head;
    m->equip[FF4_EQ_BODY] = c->eq_body;
    m->equip[FF4_EQ_FINGER] = c->eq_finger;
}

/* ==================================================================== */
/*  Interpreter                                                         */
/* ==================================================================== */

static void action_emit(FF4_EventRun *run, FF4_EventActionKind kind,
                        int param, int param2)
{
    memset(&run->last, 0, sizeof run->last);
    run->last.kind = kind;
    run->last.param = param;
    run->last.param2 = param2;
}

/* Direct state mutations (with scene requests where a host must act). */
static bool execute_command(FF4_EventRun *run, FF4_EventCmdDecoded *d)
{
    FF4_GameState *g = run->g;
    int a = d->arg_count ? d->args[0] : 0;
    int i;

    switch (d->id) {
    /* ---- party commands (applied here; result is visual) ---- */
    case FF4_EVT_PARTY_ADD: {
        int slot;

        if (a < 1 || a > 0x16)
            break;
        for (slot = 0; slot < FF4_PARTY_MAX; ++slot)
            if (g->party[slot].id == FF4_CHAR_NONE)
                break;
        if (slot >= FF4_PARTY_MAX)
            break;

    /* @e81f/e866: restore saved copy when the table demands it. */
        if (ff4_char_remove_tbl[a - 1] & 0x80) {
            int saved = ff4_char_remove_tbl[a - 1] & 0x7F;

            g->party[slot] = g->saved_char[saved];
            g->party[slot].id = a;
            g->party[slot].hp = g->party[slot].max_hp;
            g->party[slot].mp = g->party[slot].max_mp;
            /* @e848: restored adult Rydia switches class + gear */
            if (a == FF4_CHAR_RYDIA_ADULT) {
                g->party[slot].job = FF4_JOB_CALLER_ADULT;
                g->party[slot].equip[FF4_EQ_RIGHT_HAND] =
                    g->party[slot].equip[FF4_EQ_LEFT_HAND] =
                    g->party[slot].equip[FF4_EQ_HEAD] =
                    g->party[slot].equip[FF4_EQ_BODY] =
                    g->party[slot].equip[FF4_EQ_FINGER] = 0;
            }
        } else {
            load_char_prop(run->db, &g->party[slot], (uint8_t)a);
        }
        break;
    }
    case FF4_EVT_PARTY_REMOVE: {
        int slot;

        if (a < 1 || a > 0x16)
            break;
        slot = find_member(g, (uint8_t)a);
        if (slot < 0)
            break;
        /* @e962: save unless the table forbids it */
        if (!(ff4_char_add_tbl[a - 1] & 0x80)) {
            int saved = ff4_char_add_tbl[a - 1] & 0x7F;

            if (saved < 5)
                g->saved_char[saved] = g->party[slot];
        }
        memset(&g->party[slot], 0, sizeof g->party[slot]);
        break;
    }

    /* ---- switches ---- */
    case FF4_EVT_SWITCH_SET:
        set_switch(g->world.event_switches,
                   sizeof g->world.event_switches, (uint8_t)a, true);
        break;
    case FF4_EVT_SWITCH_CLEAR:
        set_switch(g->world.event_switches,
                   sizeof g->world.event_switches, (uint8_t)a, false);
        break;
    case FF4_EVT_NPC_SWITCH_SET:
        set_switch(g->world.npc_switches,
                   sizeof g->world.npc_switches, (uint8_t)a, true);
        break;
    case FF4_EVT_NPC_SWITCH_CLEAR:
        set_switch(g->world.npc_switches,
                   sizeof g->world.npc_switches, (uint8_t)a, false);
        break;

    /* ---- inventory / gold ---- */
    case FF4_EVT_ITEM_GIVE:
        if (a > 0) {
            if (!ff4_inventory_add(&g->inventory, (FF4_ItemId)a, 1)) {
                action_emit(run, FF4_EVT_ACT_DIALOGUE, -1, 0);
                run->last.text = "Can't carry!";
                return true;             /* the game blocks the event here */
            }
        }
        break;
    case FF4_EVT_ITEM_TAKE:
        ff4_inventory_remove(&g->inventory, (FF4_ItemId)a, 1);
        break;
    case FF4_EVT_GP_GIVE:
        g->inventory.gil_full += (uint32_t)a;
        break;
    case FF4_EVT_GP_TAKE:
        g->inventory.gil_full -=
            (uint32_t)(g->inventory.gil_full < (uint32_t)a
                           ? g->inventory.gil_full
                           : (uint32_t)a);
        break;

    /* ---- spells, hp/mp, statuses ---- */
    case FF4_EVT_SPELL: {
        int list = d->arg_count > 0 ? d->args[0] : 0;
        int spell = d->arg_count > 1 ? d->args[1] : 0;
        int n;

        if (list >= 0 && list < FF4_SPELL_LIST_COUNT && spell > 0) {
            for (n = 0; n < FF4_SPELLS_PER_LIST; ++n) {
                if (g->spell_tables[list][n] == 0) {
                    g->spell_tables[list][n] = (uint8_t)spell;
                    break;
                }
            }
        }
        break;
    }
    case FF4_EVT_CHAR_HP:
        for (i = 0; i < FF4_PARTY_MAX; ++i) {
            FF4_PartyMember *m = &g->party[i];

            if (m->id == FF4_CHAR_NONE || (m->status[0] & FF4_ST1_DEAD))
                continue;
            if (a == 0xFE) {
                m->hp = m->max_hp;              /* @eac7 full restore */
            } else {
                uint32_t heal = (uint32_t)a * 10u;

                if ((uint32_t)m->hp + heal > m->max_hp)
                    m->hp = m->max_hp;
                else
                    m->hp += (uint16_t)heal;
            }
        }
        break;
    case FF4_EVT_CHAR_MP:
        for (i = 0; i < FF4_PARTY_MAX; ++i) {
            FF4_PartyMember *m = &g->party[i];

            if (m->id == FF4_CHAR_NONE || (m->status[0] & FF4_ST1_DEAD))
                continue;
            if (a == 0xFE) {
                m->mp = m->max_mp;
            } else {
                uint32_t heal = (uint32_t)a * 10u;

                if ((uint32_t)m->mp + heal > m->max_mp)
                    m->mp = m->max_mp;
                else
                    m->mp += (uint16_t)heal;
            }
        }
        break;
    case FF4_EVT_STATUS_TOGGLE:
        for (i = 0; i < FF4_PARTY_MAX; ++i) {
            if (g->party[i].id != FF4_CHAR_NONE)
                g->party[i].status[0] ^= (uint8_t)a;
        }
        break;
    case FF4_EVT_STATUS_SET:
        for (i = 0; i < FF4_PARTY_MAX; ++i) {
            if (g->party[i].id != FF4_CHAR_NONE)
                g->party[i].status[0] |= (uint8_t)a;
        }
        break;
    case FF4_EVT_STATUS_CLEAR:
        /* the game ANDs the mask (whitelist semantics, event.asm @ebb9) */
        for (i = 0; i < FF4_PARTY_MAX; ++i) {
            if (g->party[i].id != FF4_CHAR_NONE)
                g->party[i].status[0] &= (uint8_t)a;
        }
        break;

    /* ---- map warp ---- */
    case FF4_EVT_MAP: {
        /* args: map id, %DDXXXXXX, y. The DD value ($1705 encoding:
           0 up 1 right 2 down 3 left) becomes the movement direction. */
        static const FF4_Direction dd_to_dir[4] = {
            FF4_DIR_UP, FF4_DIR_RIGHT, FF4_DIR_DOWN, FF4_DIR_LEFT
        };
        int dd = d->arg_count > 1 ? (d->args[1] >> 6) : 0;
        uint8_t map = d->arg_count > 0 ? d->args[0] : 0;

        if ((int)map < 0xFB) {
            /* push previous map onto the stack ($172C) */
            int prev = g->world.prev_map_count % 64;

            g->world.prev_map[prev][0] = g->world.map_id;
            g->world.prev_map[prev][1] =
                (uint8_t)((g->world.facing << 6) | (g->world.x & 0x3F));
            g->world.prev_map_count++;
            g->world.x = d->arg_count > 1 ? (d->args[1] & 0x3F) : 0;
            g->world.y = d->arg_count > 2 ? (d->args[2] & 0x7F) : 0;
            g->world.world = (d->arg_count > 2 && (d->args[2] & 0x80))
                                 ? FF4_WORLD_UNDERGROUND
                                 : FF4_WORLD_OVERWORLD;
        }
        g->world.map_id = map;
        g->world.facing = dd_to_dir[dd & 3];
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)FF4_EVT_MAP, 0);
        break;
    }

    /* ---- wait / screens / dialog / scene requests ---- */
    case FF4_EVT_WAIT:
        run->wait = (uint32_t)a * 8u;    /* @ed3d: param * 8 */
        action_emit(run, FF4_EVT_ACT_WAIT, (int)run->wait, 0);
        break;
    case FF4_EVT_NPC_DIALOG:
    case FF4_EVT_MAP_DIALOG:
        action_emit(run, FF4_EVT_ACT_DIALOGUE, a, d->id == FF4_EVT_NPC_DIALOG ? 0 : 0);
        run->last.text = ff4_event_dialog_text(run->db, 0, a);
        break;
    case FF4_EVT_DIALOG:
    case FF4_EVT_DIALOG_2:
        action_emit(run, FF4_EVT_ACT_DIALOGUE, a, 1);
        run->last.text = ff4_event_dialog_text(run->db, 1, a);
        break;
    case FF4_EVT_DIALOG_3:
        action_emit(run, FF4_EVT_ACT_DIALOGUE, a, 2);
        run->last.text = ff4_event_dialog_text(run->db, 2, a);
        break;
    case FF4_EVT_SPEED:
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)FF4_EVT_SPEED, 0);
        break;
    case FF4_EVT_MOON:
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)FF4_EVT_MOON, 0);
        break;
    case FF4_EVT_SCREEN_FADE:
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)FF4_EVT_SCREEN_FADE, a);
        break;
    /* ---- scene requests / effects ---- */
    case FF4_EVT_BATTLE:
        action_emit(run, FF4_EVT_ACT_BATTLE,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_SHOP:
        action_emit(run, FF4_EVT_ACT_SHOP,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_INN:
        action_emit(run, FF4_EVT_ACT_INN,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_NAMINGWAY:
        action_emit(run, FF4_EVT_ACT_NAMINGWAY, 0, 0);
        break;
    case FF4_EVT_BIG_CHOCOBO:
        action_emit(run, FF4_EVT_ACT_BIG_CHOCOBO, 0, 0);
        break;
    case FF4_EVT_ITEM_SELECT:
        action_emit(run, FF4_EVT_ACT_ITEM_SELECT, 0, 0);
        break;
    case FF4_EVT_YES_NO:
        action_emit(run, FF4_EVT_ACT_YESNO,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_CINEMATIC:
        action_emit(run, FF4_EVT_ACT_CINEMATIC,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_CHAR_GRAPHICS:
        action_emit(run, FF4_EVT_ACT_GFX_CHANGE,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_SONG_FADE:
    case FF4_EVT_SONG:
    case FF4_EVT_SONG_2:
        action_emit(run, FF4_EVT_ACT_SONG,
                    d->arg_count ? d->args[0] : 0,
                    d->id == FF4_EVT_SONG_FADE ? 0 : 1);
        break;
    case FF4_EVT_SOUND:
        action_emit(run, FF4_EVT_ACT_SFX,
                    d->arg_count ? d->args[0] : 0, 0);
        break;
    case FF4_EVT_SCREEN_SHAKE:
    case FF4_EVT_SCREEN_FLASH:
    case FF4_EVT_SCREEN_BLUR:
    case FF4_EVT_SCREEN_TINT:
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)d->id,
                    d->arg_count ? d->args[0] : 0);
        break;
    case FF4_EVT_DOOR:
        action_emit(run, FF4_EVT_ACT_SCREEN, (int)FF4_EVT_DOOR, 0);
        break;
    default:
        break;
    }
    return false;
}

static void finish_segment(FF4_EventRun *run)
{
    size_t p = run->seg_end + 1;

    /* skip any ff padding between sub-scripts */
    while (p < run->len && run->script[p] == 0xFF)
        ++p;
    if (p < run->len) {
        run->seg_start = p;
        run->seg_end = run->len;      /* recomputed on the walk */
        run->pc = p;
        return;
    }
    run->state = FF4_EVENT_DONE;
}

void ff4_event_run_begin(FF4_EventRun *run, FF4_GameState *g,
                         const FF4_AssetDB *db,
                         const uint8_t *script, size_t len)
{
    memset(run, 0, sizeof *run);
    run->g = g;
    run->db = db;
    run->script = script;
    run->len = len;
    run->seg_start = 0;
    run->seg_end = len;
    run->pc = 0;
    run->state = FF4_EVENT_RUNNING;
    g->world.event_running = true;
}

FF4_EventAction ff4_event_step(FF4_EventRun *run)
{
    FF4_EventCmdDecoded d;
    const uint8_t *base;
    size_t *cursor, limit;
    int r;

    run->last.kind = FF4_EVT_ACT_NONE;

    if (run->state != FF4_EVENT_RUNNING)
        return run->last;

    /* wait timer first (no command advances while waiting) */
    if (run->wait > 0) {
        action_emit(run, FF4_EVT_ACT_WAIT, (int)run->wait, 0);
        return run->last;
    }

    if (++run->safety > 100000u) {
        run->state = FF4_EVENT_DONE;   /* runaway guard */
        return run->last;
    }

    /* select the active code region (repeat body or main script) */
    if (run->rep_depth > 0) {
        FF4_RepeatFrame *f = &run->reps[run->rep_depth - 1];

        base = f->body;
        cursor = &f->pc;
        limit = f->len;
    } else {
        base = run->script + run->seg_start;
        cursor = &run->pc;
        limit = run->seg_end - run->seg_start;
    }

    if (*cursor >= limit) {
        /* repeat body exhausted: next pass or pop */
        if (run->rep_depth > 0) {
            FF4_RepeatFrame *f = &run->reps[run->rep_depth - 1];

            if (--f->remaining == 0) {
                run->rep_depth--;
                return run->last;
            }
            f->pc = 0;
            return run->last;
        }
        finish_segment(run);
        return run->last;
    }

    r = ff4_event_decode(base, limit, cursor, &d);
    if (r < 0) {
        run->state = FF4_EVENT_DONE;
        return run->last;
    }

    if (d.id == FF4_EVT_END) {
        /* end of repeat pass, or end of the whole segment */
        if (run->rep_depth > 0) {
            FF4_RepeatFrame *f = &run->reps[run->rep_depth - 1];

            if (--f->remaining == 0)
                run->rep_depth--;
            else
                f->pc = 0;
            return run->last;
        }
        run->seg_end = run->seg_start + (*cursor - 1);
        finish_segment(run);
        return run->last;
    }

    if (d.id == FF4_EVT_REPEAT) {
        /* args[0] = count, args[1] = body length; body begins at cursor */
        const uint8_t *body = base + *cursor - (size_t)d.args[1];
        size_t body_len = d.args[1];

        if (run->rep_depth < FF4_EVENT_REPEAT_DEPTH && body_len > 0) {
            FF4_RepeatFrame *f = &run->reps[run->rep_depth++];

            /* the copy path appends an implicit 0xFF end marker */
            f->body = body;
            f->len = body_len;
            f->pc = 0;
            f->remaining = d.args[0] ? d.args[0] : 1;
        }
        /* repeat with zero length is a no-op */
        return run->last;
    }

    if (d.id == FF4_EVT_REPEAT) {
        /* args[0] = count, args[1] = body length; the decoder already
           skipped past the repeat + body; body = pc - body_len. */
        const uint8_t *body = base + *cursor - (size_t)d.args[1];
        size_t body_len = d.args[1];

        if (run->rep_depth < FF4_EVENT_REPEAT_DEPTH && body_len > 0) {
            FF4_RepeatFrame *f = &run->reps[run->rep_depth++];

            /* the copy path runs the body bytes; the end marker is the
               implicit 0xFF the copier appends (event.asm @e28f) */
            f->body = body;
            f->len = body_len;
            f->pc = 0;
            f->remaining = d.args[0] ? d.args[0] : 1;
        }
        /* repeat with zero length is a no-op */
        return run->last;
    }

    execute_command(run, &d);

    /* movement actions resolve to scene requests too */
    if ((uint8_t)d.id < 0xD0 && (uint8_t)d.id <= 0xBF) {
        action_emit(run, FF4_EVT_ACT_MOVE, 0, 0);
        run->last.obj = d.obj;
        run->last.act = d.act;
    }

    /* block the run until the host acknowledges scene-requiring ops */
    switch (run->last.kind) {
    case FF4_EVT_ACT_DIALOGUE:
    case FF4_EVT_ACT_YESNO:
    case FF4_EVT_ACT_ITEM_SELECT:
    case FF4_EVT_ACT_BATTLE:
    case FF4_EVT_ACT_SHOP:
    case FF4_EVT_ACT_INN:
    case FF4_EVT_ACT_NAMINGWAY:
    case FF4_EVT_ACT_BIG_CHOCOBO:
    case FF4_EVT_ACT_CINEMATIC:
        run->host_blocked = 1;
        run->state = FF4_EVENT_BLOCKED;
        return run->last;
    default:
        break;
    }

    /* movement actions resolve to scene requests too */
    if (d.id == FF4_EVT_ACTION) {
        uint8_t op = (uint8_t)d.id;

        if (op >= 0xC4 && op <= 0xCD) {
            /* @e4bf-@e514: player facing / sprite side effects */
            if (op <= 0xC7)
                run->g->world.facing = (FF4_Direction)(op - 0xC4);
            else if (op == 0xC8)
                run->g->world.hide_player = true;
            else if (op == 0xC9)
                run->g->world.hide_player = false;
            else if (op == 0xCA)
                run->g->world.facing = (FF4_Direction)5;
            else if (op == 0xCB)
                run->g->world.facing = (FF4_Direction)4;
            else if (op == 0xCC)
                run->g->world.facing = (FF4_Direction)6;
            else if (op == 0xCD)
                run->g->world.facing = (FF4_Direction)7;
        } else if (op == 0xCE) {
            run->g->world.event_running = !run->g->world.event_running; /* $cf */
        } else if (op == 0xCF) {
            run->g->world.spin_move = !run->g->world.spin_move;         /* $d8 */
        }
    }

    return run->last;
}

void ff4_event_ack(FF4_EventRun *run)
{
    run->host_blocked = 0;
    if (run->state == FF4_EVENT_BLOCKED)
        run->state = FF4_EVENT_RUNNING;
}

void ff4_event_tick(FF4_EventRun *run, uint32_t frames)
{
    if (run->state != FF4_EVENT_RUNNING && run->state != FF4_EVENT_BLOCKED)
        return;
    if (run->wait > 0) {
        if (run->wait > frames)
            run->wait -= frames;
        else
            run->wait = 0;
    }
}