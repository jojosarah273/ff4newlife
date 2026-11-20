/**
 * @file event_engine.h
 * @brief Phase 4: event script engine & interpreter.
 *
 * Reproduces the game's event VM (field/event.asm):
 *
 *   - scripts live as byte blobs (256 events, ROM $12:8000+; ripped into
 *     obj.eventScript, each may pack several concatenated sub-scripts);
 *   - commands decode with the *copier* table (event.asm @e206-@e28f):
 *       0x00-0xCF  1-byte movement/action command (nibble encoding)
 *       0xD0-0xDA  no parameters (screen fx, vehicle/door toggles)
 *       0xDB-0xE1  1 parameter  (item/gp/status/hp/mp/toggle ops)
 *       0xE2       2 parameters (learn spell: list id + spell id)
 *       0xE3-0xFD  1 parameter  (switches, dialogs, song, battle, shop...)
 *       0xEB       repeat: count + length, then the repeated body
 *       0xFE       3 parameters (warp: map id, packed dir+x, y)
 *       0xFF       end
 *   This exact table round-trips all 256 ripped scripts (validated).
 *
 * The interpreter mutates game state directly (switches, party, items,
 * gil, spells, hp/mp, statuses, warps) and emits *scene requests* (an
 * action struct) for everything a host must visualize: dialogue windows,
 * battles, shops, inns, songs, screen effects, movement of NPCs. The host
 * (field scene, Phase 6) completes the request via ff4_event_ack().
 *
 * Quirks preserved: party save/restore slots (CharAddTbl/CharRemoveTbl),
 * adult-Rydia class swap, HP/MP restore x10 (or 0xFE = full), status
 * toggle XOR / "clear" AND whitelist.
 */
#ifndef FF4_EVENT_ENGINE_H
#define FF4_EVENT_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  Command set (opcode values = the byte values in scripts)            */
/* ==================================================================== */

typedef enum FF4_EventCmd {
    FF4_EVT_ACTION = 0x00,           /* 0x00-0xCF movement actions */

    FF4_EVT_SCREEN_SHAKE = 0xD0,
    FF4_EVT_SCREEN_FLASH = 0xD1,
    FF4_EVT_SCREEN_BLUR = 0xD2,      /* mosaic/blur */
    FF4_EVT_MOON = 0xD3,             /* enable moon travel gate */
    FF4_EVT_BIG_CHOCOBO = 0xD4,      /* fat chocobo item store menu */
    FF4_EVT_DOOR = 0xD5,             /* open nearby secret door */
    FF4_EVT_D6 = 0xD6,               /* unused */
    FF4_EVT_SPEED = 0xD7,            /* toggle dash (fast movement) */
    FF4_EVT_SONG_FADE = 0xD8,        /* fade out music (slow) */
    FF4_EVT_NAMINGWAY = 0xD9,        /* rename character menu */
    FF4_EVT_SCREEN_FADE = 0xDA,      /* fade in/out */

    FF4_EVT_STATUS_TOGGLE = 0xDB,    /* party status1 ^= param */
    FF4_EVT_INN = 0xDC,              /* inn: sleep, param = price */
    FF4_EVT_CHAR_GRAPHICS = 0xDD,    /* change character sprite */
    FF4_EVT_CHAR_HP = 0xDE,          /* restore hp (param*10, 0xFE=full) */
    FF4_EVT_CHAR_MP = 0xDF,          /* restore mp */
    FF4_EVT_ITEM_GIVE = 0xE0,        /* give item */
    FF4_EVT_ITEM_TAKE = 0xE1,        /* take item */
    FF4_EVT_SPELL = 0xE2,            /* learn spell (list id, spell id) */
    FF4_EVT_STATUS_CLEAR = 0xE3,     /* party status1 &= param (whitelist) */
    FF4_EVT_STATUS_SET = 0xE4,       /* party status1 |= param */
    FF4_EVT_GP_GIVE = 0xE5,          /* give gil */
    FF4_EVT_GP_TAKE = 0xE6,          /* take gil */
    FF4_EVT_PARTY_ADD = 0xE7,        /* add character */
    FF4_EVT_PARTY_REMOVE = 0xE8,     /* remove character */
    FF4_EVT_WAIT = 0xE9,             /* wait frames (param * 8) */
    FF4_EVT_SONG = 0xEA,             /* play song (with fade in) */
    FF4_EVT_REPEAT = 0xEB,           /* repeat body count times */
    FF4_EVT_BATTLE = 0xEC,           /* start battle (formation id) */
    FF4_EVT_SHOP = 0xED,             /* enter shop */
    FF4_EVT_NPC_DIALOG = 0xEE,       /* event dialog bank 0 */
    FF4_EVT_MAP_DIALOG = 0xEF,       /* map dialog (bank 0) */
    FF4_EVT_DIALOG = 0xF0,           /* event dialog bank 1 */
    FF4_EVT_DIALOG_2 = 0xF1,         /* event dialog bank 1 continued */
    FF4_EVT_SWITCH_SET = 0xF2,       /* set event switch */
    FF4_EVT_SWITCH_CLEAR = 0xF3,     /* clear event switch */
    FF4_EVT_NPC_SWITCH_SET = 0xF4,   /* set npc switch */
    FF4_EVT_NPC_SWITCH_CLEAR = 0xF5, /* clear npc switch */
    FF4_EVT_DIALOG_3 = 0xF6,         /* event dialog bank 2 */
    FF4_EVT_ITEM_SELECT = 0xF7,      /* item selection menu */
    FF4_EVT_YES_NO = 0xF8,           /* yes/no dialogue */
    FF4_EVT_SCREEN_TINT = 0xF9,      /* tint screen colour */
    FF4_EVT_SONG_2 = 0xFA,           /* play song */
    FF4_EVT_SOUND = 0xFB,            /* play sound effect */
    FF4_EVT_FC = 0xFC,               /* unused */
    FF4_EVT_CINEMATIC = 0xFD,        /* cutscene (intro/credits) */
    FF4_EVT_MAP = 0xFE,              /* warp (map, packed dir+x, y) */
    FF4_EVT_END = 0xFF
} FF4_EventCmd;

/** One decoded instruction. */
typedef struct FF4_EventCmdDecoded {
    FF4_EventCmd id;
    int obj;                  /**< action: object nibble (0xF0 mask). */
    int act;                  /**< action: action nibble (0x0F mask). */
    uint8_t args[3];          /**< Command parameters. */
    int arg_count;
    size_t next;              /**< Stream offset after this instruction. */
} FF4_EventCmdDecoded;

/**
 * Decode one instruction from a script at *pos using the copier table.
 * Returns 0 on success, -1 on malformed input. Updates *pos.
 */
int ff4_event_decode(const uint8_t *buf, size_t len, size_t *pos,
                     FF4_EventCmdDecoded *out);

/* ==================================================================== */
/*  Scene requests (what the engine asks the host to do)                */
/* ==================================================================== */

typedef enum FF4_EventActionKind {
    FF4_EVT_ACT_NONE = 0,
    FF4_EVT_ACT_MOVE,        /**< action cmd: obj moves/walks (field scene). */
    FF4_EVT_ACT_DIALOGUE,    /**< Open dialogue; param = text id (table by kind). */
    FF4_EVT_ACT_YESNO,       /**< Yes/no prompt. */
    FF4_EVT_ACT_ITEM_SELECT, /**< Item selection (param = usage context). */
    FF4_EVT_ACT_BATTLE,      /**< Enter battle scene; param = formation id. */
    FF4_EVT_ACT_SHOP,        /**< Enter shop; param = shop id. */
    FF4_EVT_ACT_INN,         /**< Inn; param = price. */
    FF4_EVT_ACT_NAMINGWAY,   /**< Rename menu. */
    FF4_EVT_ACT_BIG_CHOCOBO, /**< Fat chocobo item storage. */
    FF4_EVT_ACT_CINEMATIC,   /**< Cutscene; param = cinematic id. */
    FF4_EVT_ACT_SONG,        /**< Play BGM; param = track id, param2 = fade. */
    FF4_EVT_ACT_SFX,         /**< Play SFX; param = sfx id. */
    FF4_EVT_ACT_SCREEN,      /**< Screen effect; param = d0-d2/d9/da/d9 kinds. */
    FF4_EVT_ACT_GFX_CHANGE,  /**< Character sprite swap (field scene). */
    FF4_EVT_ACT_WAIT         /**< Waiting (frames left in run->wait). */
} FF4_EventActionKind;

typedef struct FF4_EventAction {
    FF4_EventActionKind kind;
    int param;               /**< id (dialog/battle/shop/song/effect...). */
    int param2;              /**< secondary id (dialog bank, fade mode). */
    int obj, act;            /**< For FF4_EVT_ACT_MOVE. */
    const char *text;        /**< Resolved dialogue text (if any). */
} FF4_EventAction;

/* ==================================================================== */
/*  Interpreter run                                                     */
/* ==================================================================== */

#define FF4_EVENT_REPEAT_DEPTH 4

typedef struct FF4_RepeatFrame {
    const uint8_t *body;      /**< Repeat body start (into the script). */
    size_t len;               /**< Body length in bytes. */
    size_t pc;                /**< Cursor inside the body. */
    uint8_t remaining;        /**< Passes left (incl. current). */
} FF4_RepeatFrame;

typedef enum FF4_EventRunState {
    FF4_EVENT_RUNNING = 0,   /**< Executes one command per step. */
    FF4_EVENT_BLOCKED,       /**< Waiting on a host request or wait timer. */
    FF4_EVENT_DONE           /**< All sub-scripts executed. */
} FF4_EventRunState;

typedef struct FF4_EventRun {
    FF4_GameState *g;
    const FF4_AssetDB *db;

    const uint8_t *script;    /**< Raw bytes (may pack several scripts). */
    size_t len;

    size_t seg_start;         /**< Current sub-script start offset. */
    size_t seg_end;           /**< Current sub-script end (the 0xFF). */

    size_t pc;                /**< Cursor (inside repeat body if depth > 0). */
    FF4_RepeatFrame reps[FF4_EVENT_REPEAT_DEPTH];
    int rep_depth;

    uint32_t wait;            /**< Wait frames remaining (0 = none). */
    int host_blocked;         /**< Awaiting ff4_event_ack(). */

    FF4_EventRunState state;
    FF4_EventAction last;     /**< Action emitted by the latest step. */

    uint32_t safety;          /**< Executed-instruction counter (runaway guard). */
} FF4_EventRun;

/**
 * Start running an event script. `script` may be the raw blob for one
 * (or several concatenated) sub-scripts; the run walks each in turn.
 */
void ff4_event_run_begin(FF4_EventRun *run, FF4_GameState *g,
                         const FF4_AssetDB *db,
                         const uint8_t *script, size_t len);

/**
 * Execute one command. Applies state mutations immediately and stores the
 * scene request (if any) in run->last. Returns the emitted action.
 * When the action requires a blocked host operation, run->state becomes
 * FF4_EVENT_BLOCKED; call ff4_event_ack() once it is finished.
 */
FF4_EventAction ff4_event_step(FF4_EventRun *run);

/** Host acknowledges the blocked request (dialogue closed, battle over...). */
void ff4_event_ack(FF4_EventRun *run);

/** Advance wait timers; unblocks when the wait elapses. */
void ff4_event_tick(FF4_EventRun *run, uint32_t frames);

/* Convenience: resolve a dialogue string from the db for a given command
   (bank 0 = mapDialog, bank 1 = eventDialog1, bank 2 = eventDialog2). */
const char *ff4_event_dialog_text(const FF4_AssetDB *db, int bank, int id);

/* Party save/restore slot tables (char id - 1 index, from event.asm). */
extern const uint8_t ff4_char_add_tbl[21];
extern const uint8_t ff4_char_remove_tbl[21];

#ifdef __cplusplus
}
#endif

#endif /* FF4_EVENT_ENGINE_H */