/**
 * @file battle_engine.h
 * @brief Phase 5: battle system engine.
 *
 * The battle *scene*: formation assembly, ATB scheduling, command
 * execution and monster AI - ported from the disassembly's battle/
 * modules (battle.asm, init.asm, timer.asm, fight.asm, magic.asm,
 * damage.asm, ai.asm). Mechanics come from ff4_math; data from the
 * asset db.
 *
 * Scene shape (mirrors the game):
 *   ff4_battle_start   -> assemble the formation into FF4_BattleState
 *   ff4_battle_atb     -> per battle-speed tick: advance ATB + status
 *                          timers (port of DecTimers)
 *   ff4_battle_run     -> the round loop: pending action -> execute ->
 *                          status updates -> check end (port of the
 *                          BattleMain / DoAction state machine)
 *   ff4_battle_dispatch -> the four action types (GetCharAttack /
 *                          GetMonsterAttack / DoAttack / DoTimerEffect)
 *
 * Rewards (exp/gp/drops) are computed at victory; the caller finishes
 * the scene and returns to the field.
 */
#ifndef FF4_BATTLE_ENGINE_H
#define FF4_BATTLE_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/core/ff4_math.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  Battle setup                                                        */
/* ==================================================================== */

typedef struct FF4_BattleOpts {
    bool back_attack;       /**< $3581: monsters strike first. */
    bool first_strike;      /**< $38d7: pre-emptive strike (msg). */
    bool forced_escape;     /**< $29A2 bit0: cannot escape. */
    uint8_t battle_bg;      /**< $1802: battle background id. */
    uint8_t music;          /**< $29A2: battle music select. */
} FF4_BattleOpts;

/**
 * Start a battle from a formation id. Populates g.battle (monsters,
 * party copies, ATB bases, timers). Returns 0 on success.
 */
int ff4_battle_start(const FF4_AssetDB *db, FF4_GameState *g,
                     uint8_t formation_id, const FF4_BattleOpts *opts);

/* ==================================================================== */
/*  ATB / timers                                                        */
/* ==================================================================== */

/**
 * Advance timers for the given frames (battle speed handled by the
 * caller's frame governor): one frame of the game's "battle speed".
 * Ports DecTimers: decrements enabled ATB (and poison/sap/doom/stop)
 * timers; filling the gauge marks the combatant ready.
 */
void ff4_battle_atb(const FF4_AssetDB *db, FF4_GameState *g, uint32_t frames);

/** True when a combatant's ATB gauge is full (pending action). */
bool ff4_battle_ready(const FF4_BattleState *b, int slot);

/**
 * Round-robin pending scan (GetPendingAction): starting after
 * `after_slot`, return the next combatant ready to act, or -1.
 */
int ff4_battle_next_ready(const FF4_BattleState *b, int after_slot);

/* ==================================================================== */
/*  Command selection (character side)                                  */
/* ==================================================================== */

/** Queue a character's command for execution (the game's $2051 block). */
void ff4_battle_set_command(FF4_BattleState *b, int slot, FF4_BattleCommand cmd,
                            uint8_t param, uint8_t target_chars,
                            uint8_t target_mons);

/** Default auto command for a ready character (Fight). */
void ff4_battle_auto_command(FF4_BattleState *b, int slot);

/* ==================================================================== */
/*  Execution                                                           */
/* ==================================================================== */

/**
 * Execute the queued action of the given combatant (DoAttack stage).
 * Handles Fight / Item / magic (spell id = attack id, summons mapped
 * via GetSummonAttack) and monster attack ids from the AI. Damage and
 * statuses are applied to the battle damage buffers.
 */
void ff4_battle_execute(FF4_Rng *rng, const FF4_AssetDB *db,
                        FF4_GameState *g, int slot);

/**
 * Monster AI: pick the monster's next attack id (interprets the
 * condition/action script tables; basic subset of ai.asm).
 */
uint8_t ff4_monster_ai_pick(FF4_Rng *rng, const FF4_AssetDB *db,
                            const FF4_Monster *m);

/** Apply damage/heal to a combatant and record it in the buffers. */
void ff4_battle_apply_damage(FF4_BattleState *b, int slot, int amount,
                             bool heal);

/**
 * End-of-round updates (UpdateDead port): apply pending statuses, kill
 * dead combatants, update monster type counts and the total.
 */
void ff4_battle_update_dead(FF4_BattleState *b);

/**
 * Status-linked timer effects at each battle tick (DecTimers port):
 * poison/sap hit max_hp/8, doom counts down, stop/sleep/paralyze gate
 * the ATB. Returns after processing.
 */
void ff4_battle_timer_effects(FF4_GameState *g, uint32_t frames);

/* ==================================================================== */
/*  Battle end                                                          */
/* ==================================================================== */

/** Victory: all monsters dead. Defeat: all characters dead. */
bool ff4_battle_check_end(const FF4_BattleState *b, bool *defeat);

/**
 * Apply victory rewards: exp (via level-up), gil, and rolled item
 * drops, then mark the battle finished.
 */
void ff4_battle_win_rewards(FF4_Rng *rng, const FF4_AssetDB *db,
                            FF4_GameState *g);

/** Reset the battle state back to field mode. */
void ff4_battle_finish(FF4_BattleState *b);

/**
 * Copy the battle party records (hp/mp/status) back into the field
 * party (the game's RestoreCharProp at battle end).
 */
void ff4_battle_sync_back(FF4_GameState *g);

#ifdef __cplusplus
}
#endif

#endif /* FF4_BATTLE_ENGINE_H */