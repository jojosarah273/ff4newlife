/**
 * @file ff4_math.h
 * @brief Phase 3: core game math & mechanics.
 *
 * Faithful C ports of the game's battle math, lifted from the disassembly
 * (everything8215/ff4, battle module sources). Everything the SNES code
 * here - quirks, off-by-ones and all (noted in comments with asm anchors):
 *
 *   - RandXA / Rand99 / Rand  (battle/battle.asm @8379)
 *   - Mult16 / Div16         (battle/battle.asm @83b9/@8407)
 *   - CalcDmg / ApplyDmgMult (battle/damage.asm @c99f/@ca41)
 *   - physical attack chain  (battle/fight.asm @c4fb-@c7fb)
 *   - elemental/race mults   (battle/fight.asm @c62c-@c685)
 *   - stat calculation       (battle/equip.asm UpdateCharStats @9837
 *                             + documented formulas)
 *   - status application     (battle/magic.asm @d53c, battle/attack.asm
 *                             UpdateDead @b1a0)
 */
#ifndef FF4_MATH_H
#define FF4_MATH_H

#include <stdbool.h>
#include <stdint.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  RNG (RandXA / Rand99 / Rand)                                        */
/* ==================================================================== */

/** Sequence RNG: 256-byte table + rollover byte pointer ($97/$1900). */
typedef struct FF4_Rng {
    uint8_t table[256];
    uint8_t ptr;            /**< Current table index ($97). */
} FF4_Rng;

void ff4_rng_init(FF4_Rng *rng, const uint8_t table[256]);
void ff4_rng_seed(FF4_Rng *rng, uint8_t seed);

/**
 * RandXA: random in [lo, hi] with the original's edge behaviour:
 *   - lo == 0xFF          -> returns hi, no table byte consumed
 *   - hi == 0             -> returns 0, no byte consumed
 *   - hi == lo            -> returns hi, no byte consumed
 *   - range 256           -> raw table byte
 *   - otherwise           -> lo + table[ptr++] % (hi - lo + 1)
 */
int ff4_rng_range(FF4_Rng *rng, int lo, int hi);
int ff4_rng_rand99(FF4_Rng *rng);          /**< Rand99: [0,98]. */
int ff4_rng_rand255(FF4_Rng *rng);         /**< Rand: [0,255]. */

/* ==================================================================== */
/*  16-bit arithmetic (Mult16 / Div16)                                  */
/* ==================================================================== */

/**
 * Mult16: $393D * $393F -> 32-bit product. Note the original computes
 * damage that can overflow 16 bits and clamps at 0xFFFF in callers.
 */
uint32_t ff4_mul16(uint16_t a, uint16_t b);

/**
 * Div16: $3945 / $3947 -> quotient ($3949) + remainder ($394B).
 * (The RAM map labels $3949 "remainder"; the code accumulates the
 * quotient there - we follow the code.)
 */
void ff4_div16(uint16_t dividend, uint16_t divisor,
               uint16_t *quotient, uint16_t *remainder);

/* ==================================================================== */
/*  Damage                                                              */
/* ==================================================================== */

/**
 * ApplyDmgMult (damage.asm @ca41): multiplier byte semantics.
 *   0 -> 0, 1 -> /2, n -> x n.
 * "1x" in the game is the byte value 2 (both multipliers default to 2).
 */
int ff4_apply_dmg_mult(int damage, int mult_byte);

/** Scratch for one damage evaluation (the $390x/$38fx cells). */
typedef struct FF4_DamageCtx {
    int base_attack;     /**< $3902 attack power. */
    int defense;         /**< $3904 target defense. */
    int element_mult;    /**< $38fe (2 = neutral). */
    int race_mult;       /**< $38ff (2 = neutral). */
    int crit;            /**< $3900 critical hit flag. */
    int crit_bonus;      /**< $3901 critical bonus. */
    int net_hits;        /**< $38fc hits that connected. */
    int target_count;    /**< $3906 targets to split damage across. */
} FF4_DamageCtx;

/**
 * CalcDmg (damage.asm @c99f): the shared damage core.
 *   base = attack/2 + rng(0, attack/2-1)     (attack/2 capped at 255)
 *   dmg  = (base * elemMult * raceMult + crit_bonus - defense)
 *        * net_hits / target_count
 *   dmg = clamp(min 1, max 9999)
 */
int ff4_calc_damage(FF4_Rng *rng, const FF4_DamageCtx *ctx);

/**
 * Effective physical hit/defence rate for a combatant pair
 * (fight.asm @c4fb-@c55b). Returns the attack rate used for hit rolls.
 */
int ff4_phys_hit_rate(const FF4_PartyMember *attacker,
                      const FF4_PartyMember *target, int attacker_slot);
int ff4_phys_def_rate(const FF4_PartyMember *target, int target_slot);

/**
 * Number of hits that land: rolled hits (attacker mult) minus
 * defended hits (target def mult), mirroring CalcHits usage.
 * A negative/clamped result is a miss (returns 0).
 */
int ff4_phys_hits(FF4_Rng *rng, const FF4_PartyMember *attacker,
                  const FF4_PartyMember *target, int attacker_slot,
                  int target_slot);

/**
 * Elemental multiplier byte for an attack element vs a target
 * (fight.asm @c62c-@c685):
 *   very-weak (0x21 set) -> 8 (4x), weak (0x20) -> 4 (2x),
 *   immune (0x26)        -> 0, strong (0x25) -> 1 (1/2x), else 2 (1x).
 * (Reading the weakness bytes exactly as the code does: defense = $2720
 * for weak, $2721 for very weak, $2725 strong, $2726 immune; character
 * defence uses $2720-style layout, monsters the same record offsets.)
 */
int ff4_calc_element_mult(const FF4_PartyMember *target, uint8_t attack_element);

/**
 * Creature-type multiplier (fight.asm @c685-@c6ad):
 * character attacking a monster: +4x if attack_race covers target race;
 * monster attacking a character: 1/2x if character's race_defense covers
 * the monster's race; else neutral (2).
 */
int ff4_calc_race_mult(const FF4_PartyMember *attacker,
                       const FF4_PartyMember *target);

/**
 * One physical attack (fight.asm chain). Returns damage dealt (>=1),
 * or 0 for a miss. attacker_slot/target_slot are the 13-slot combatant
 * indices (0-4 chars, 5-12 monsters) - slot 1 gets the +25% bonus.
 */
int ff4_phys_damage(FF4_Rng *rng, const FF4_PartyMember *attacker,
                    const FF4_PartyMember *target,
                    int attacker_slot, int target_slot);

/** Spell attack power setup (magic.asm @d039): power x4, summons x8. */
int ff4_magic_attack_power(int base_power, bool summon);

/* ==================================================================== */
/*  Stat calculation (UpdateCharStats + documented formulas)            */
/* ==================================================================== */

/** Equipment stat modifier table (EquipStatTbl, battle_data.asm @feac). */
extern const int8_t ff4_equip_stat_tbl[8][2];

/**
 * Recalculate a member's derived stats from base stats + equipment
 * (battle/equip.asm UpdateCharStats). Reads item data from the asset db.
 * Preserves the natural clamp bug: modified stat >= 0xB6 -> 1 (not
 * just true negatives - unsigned 182..255 also collapse to 1).
 */
void ff4_calc_stats(const FF4_AssetDB *db, FF4_PartyMember *m);

/** Stat gain for an item's stats byte (satipbbb, per EquipStatTbl). */
int ff4_equip_stat(const FF4_AssetDB *db, uint8_t item_id, FF4_StatId stat);

/* ==================================================================== */
/*  Statuses                                                            */
/* ==================================================================== */

/**
 * Apply statuses to a target, honouring immunity ($102B/$102C layout)
 * and the dead/stone gating (magic.asm @d004/d017, @d53c):
 *   - immunity: attack status bits ANDed against target immunity skip.
 *   - dead/stone targets reject everything except Life ($0A) and the
 *     like; handled by the caller's effect dispatch, but the gating for
 *     non-revive statuses is replicated here through revive_flag.
 * Returns false if the status was rejected.
 */
bool ff4_inflict_status(FF4_PartyMember *target, uint8_t st1, uint8_t st2,
                        uint8_t st3, uint8_t st4, bool revive_flag);

#ifdef __cplusplus
}
#endif

#endif /* FF4_MATH_H */