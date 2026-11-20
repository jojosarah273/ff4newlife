/**
 * @file ff4_math.c
 * @brief Phase 3: core game math & mechanics (faithful ports).
 *
 * Every routine is a direct C rendering of the corresponding 65C816 code;
 * quirks are reproduced deliberately (comment anchors cite the asm
 * locations). Bugs the original shipped are bugs we keep.
 */
#include <stdint.h>
#include <string.h>

#include "ff4/core/ff4_math.h"

/* ==================================================================== */
/*  RNG (RandXA @8379, Rand99 @858b, Rand @8593)                        */
/* ==================================================================== */

void ff4_rng_init(FF4_Rng *rng, const uint8_t table[256])
{
    memcpy(rng->table, table, 256);
    rng->ptr = 0;
}

void ff4_rng_seed(FF4_Rng *rng, uint8_t seed)
{
    rng->ptr = seed;
}

int ff4_rng_range(FF4_Rng *rng, int lo, int hi)
{
    /* @8379: stx $96 / cpx #$ff -> return hi without consuming */
    if (lo == 0xFF)
        return hi;
    /* @8383: cmp #$00 -> return 0 without consuming */
    if (hi == 0)
        return 0;
    /* cmp $96 ($96 == hi) -> return hi without consuming */
    if (hi == lo)
        return hi;

    {
        int range = hi - lo + 1;
        uint8_t raw = rng->table[rng->ptr++];      /* inc $97 wraps at 256 */

        /* range 256 -> table byte used as-is */
        if (range >= 256)
            return raw;
        return lo + raw % range;                   /* Div16 quotient + low */
    }
}

int ff4_rng_rand99(FF4_Rng *rng)
{
    return ff4_rng_range(rng, 0, 98);
}

int ff4_rng_rand255(FF4_Rng *rng)
{
    return ff4_rng_range(rng, 0, 255);
}

/* ==================================================================== */
/*  Mult16 (@83b9) / Div16 (@8407)                                      */
/* ==================================================================== */

uint32_t ff4_mul16(uint16_t a, uint16_t b)
{
    return (uint32_t)a * (uint32_t)b;
}

void ff4_div16(uint16_t dividend, uint16_t divisor,
               uint16_t *quotient, uint16_t *remainder)
{
    uint32_t rem = 0;
    uint16_t quot = 0;
    int i;

    if (quotient)  *quotient = 0;
    if (remainder) *remainder = 0;
    if (dividend == 0 || divisor == 0)
        return;                                  /* @843f leaves cells zero */

    /* Non-restoring 16-bit division: step dividend/remainder, write the
       quotient bit into $3949 equivalent as the asm's rol chain does. */
    for (i = 0; i < 16; ++i) {
        rem = (rem << 1) | (uint32_t)((dividend >> 15) & 1);
        dividend <<= 1;
        if (rem >= divisor) {
            rem -= divisor;
            quot = (uint16_t)((quot << 1) | 1u);
        } else {
            quot <<= 1;
        }
    }
    if (quotient)  *quotient = quot;
    if (remainder) *remainder = (uint16_t)rem;
}

/* ==================================================================== */
/*  ApplyDmgMult (@ca41) / CalcDmg (@c99f)                              */
/* ==================================================================== */

int ff4_apply_dmg_mult(int damage, int mult_byte)
{
    if (mult_byte == 0)
        return 0;                                /* stz $a4 */

    /* A == 1 -> shift right (1/2); A >= 2 -> multiply */
    if (mult_byte == 1)
        return damage / 2;
    return (int)((uint32_t)damage * (uint32_t)mult_byte);
}

int ff4_calc_damage(FF4_Rng *rng, const FF4_DamageCtx *ctx)
{
    int n = ctx->base_attack / 2;
    int dmg;
    uint32_t scaled;
    uint16_t q, r;

    /* @c9b9: attack/2 clamped to 255 when it overflows a byte */
    if (n > 255)
        n = 255;

    /* @c9b9..@c9fb: base = n + RandXA(0, n) -> [n, 2n] (RandXA(0,0)=0) */
    dmg = n + ff4_rng_range(rng, 0, n ? n - 1 : 0);
    /* (with n == 0 RandXA(0,0) hits the hi==lo branch and returns 0) */

    dmg = ff4_apply_dmg_mult(dmg, ctx->element_mult);
    dmg = ff4_apply_dmg_mult(dmg, ctx->race_mult);

    if (ctx->crit)
        dmg += ctx->crit_bonus;                  /* @c9ec add crit bonus */

    dmg -= ctx->defense;                         /* @c9ec sbc $3904 */
    if (dmg < 0)
        dmg = 0;                                 /* bcs clr */

    /* damage * net_hits, clamp at 0xFFFF (@ca01) */
    scaled = ff4_mul16((uint16_t)dmg, (uint16_t)ctx->net_hits);
    if ((scaled >> 16) != 0)
        scaled = 0xFFFF;

    /* / target_count (@ca1b) */
    if (ctx->target_count > 1) {
        ff4_div16((uint16_t)(scaled & 0xFFFF), (uint16_t)ctx->target_count,
                  &q, &r);
        dmg = q;
    } else {
        dmg = (int)(scaled & 0xFFFF);
    }

    if (dmg > 9999)
        dmg = 9999;                              /* @ca3a cap 0x270f */
    if (dmg == 0)
        dmg = 1;                                 /* @ca40 min 1 */
    return dmg;
}

/* ==================================================================== */
/*  Physical attack chain (fight.asm)                                   */
/* ==================================================================== */

/* Mult8 x5 then >>2 == *5/4 (slot-1 bonus @c526/@c5af). */
static int rate_boost(int rate, int slot)
{
    if (slot == 0)
        rate = (rate * 5) / 4;
    return rate;
}

int ff4_phys_hit_rate(const FF4_PartyMember *attacker,
                      const FF4_PartyMember *target, int attacker_slot)
{
    int rate = attacker->hit_rate;               /* $269c */

    if (attacker->status[0] & FF4_ST1_BLIND)
        rate /= 2;                               /* @c510 */
    if (attacker->back_row && !(attacker->status[2] & FF4_ST3_JUMP) &&
        !attacker->long_range)
        rate /= 2;                               /* @c510 back row block */

    /* attacker rate halved again when target is in the back row
       (@c55b, unless attacker jumps or is long-range) */
    if (target->back_row && !attacker->long_range &&
        !(attacker->status[2] & FF4_ST3_JUMP))
        rate /= 2;

    /* slot-1 +25% happens last, exactly as the code stacks it */
    return rate_boost(rate, attacker_slot);
}

int ff4_phys_def_rate(const FF4_PartyMember *target, int target_slot)
{
    int rate = target->def_rate;                 /* $2729 */

    if (target->status[0] & FF4_ST1_BLIND)
        rate /= 2;                               /* @c59f..@c5af */
    return rate_boost(rate, target_slot);
}

/* CalcHits (@c987): roll `base_hits` with chance `hit_rate`. */
static int roll_hits(FF4_Rng *rng, int base_hits, int hit_rate)
{
    int i, hits = 0;

    if (base_hits <= 0)
        return 0;
    for (i = 0; i < base_hits; ++i)
        if (ff4_rng_rand99(rng) < hit_rate)      /* bcs skips the increment */
            ++hits;
    return hits;
}

int ff4_phys_hits(FF4_Rng *rng, const FF4_PartyMember *attacker,
                  const FF4_PartyMember *target, int attacker_slot,
                  int target_slot)
{
    int landed, defended = 0, net;

    /* barrier: no hits get through (@c59b) */
    if (target->status[3] & FF4_ST4_BARRIER)
        return 0;
    /* image: all defended, one image charge is consumed (@c579) */
    if (target->status[3] & (FF4_ST4_IMAGE_1 | FF4_ST4_IMAGE_2))
        return 0;

    landed = roll_hits(rng, attacker->attack_mult,
                       ff4_phys_hit_rate(attacker, target, attacker_slot));
    if (target->def_mult > 0)
        defended = roll_hits(rng, target->def_mult,
                             ff4_phys_def_rate(target, target_slot));

    net = landed - defended;                     /* @c5ea sbc */
    return net > 0 ? net : 0;                    /* miss -> zero net */
}

int ff4_calc_element_mult(const FF4_PartyMember *target, uint8_t attack_element)
{
    /* fight.asm @c62c..@c685 - identical comparison order */
    if ((attack_element & target->very_weak_elements) != 0)
        return 8;                                /* 4x @c65a */
    if ((attack_element & target->weak_elements) != 0)
        return 4;                                /* 2x @c669 */
    if ((attack_element & target->immune_elements) != 0)
        return 0;                                /* 0x @c678 */
    if ((attack_element & target->strong_elements) != 0)
        return 1;                                /* 1/2x @c685 */
    return 2;                                    /* 1x (default) */
}

int ff4_calc_race_mult(const FF4_PartyMember *attacker,
                       const FF4_PartyMember *target)
{
    /* character vs monster (@c69c off) */
    if (attacker->creature_type == 0) {
        if (target->creature_type != 0 &&
            (attacker->attack_race & target->creature_type) != 0)
            return 8;                            /* 4x */
    } else {
        /* monster vs character (@c69c on) */
        if ((target->race_defense & attacker->creature_type) != 0)
            return 1;                            /* 1/2x */
    }
    return 2;
}

int ff4_phys_damage(FF4_Rng *rng, const FF4_PartyMember *attacker,
                    const FF4_PartyMember *target,
                    int attacker_slot, int target_slot)
{
    FF4_DamageCtx ctx;
    int attack = attacker->attack_power;         /* $269d */
    int defense = target->def;                   /* $272a */

    memset(&ctx, 0, sizeof ctx);

    /* attacker size modifiers (@c6ad..@c6f1) */
    if (attacker->status[1] & FF4_ST2_BERSERK)
        attack += attack / 2;                    /* +50% */
    if (attacker->status[2] & (FF4_ST3_JUMP | FF4_ST3_FOCUS))
        attack *= 2;                             /* +100% */
    if (attacker->status[1] & FF4_ST2_CURSE)
        attack /= 2;                             /* -50% */

    /* target defence modifiers (@c6f1..@c730) */
    if (target->back_row)
        defense *= 2;                            /* @c703 */
    if (target->status[1] & FF4_ST2_CURSE)
        defense /= 2;                            /* @c710 */

    /* crit roll (@c710) - derived crit rate, stored via calc_stats */
    if (ff4_rng_rand99(rng) < attacker->crit_rate)
        ctx.crit = 1;

    if (target->status[2] & FF4_ST3_DEFEND)
        defense *= 2;                            /* @c723 */
    if (target->status[2] & FF4_ST3_FOCUS)
        defense /= 2;                            /* @c730 */

    /* toad/mini attacker: attack = 1, no crits (@c73d) */
    if (attacker->status[0] & (FF4_ST1_TOAD | FF4_ST1_MINI)) {
        attack = 1;
        ctx.crit = 0;
    }
    /* toad/mini target: attack doubles, defence zeroed (@c74d) */
    if (target->status[0] & (FF4_ST1_TOAD | FF4_ST1_MINI)) {
        attack *= 2;
        defense = 0;
    }

    ctx.base_attack = attack;
    ctx.defense = defense;
    ctx.crit_bonus = attacker->crit_bonus;       /* $26ae */
    ctx.element_mult = ff4_calc_element_mult(target, attacker->attack_element);
    ctx.race_mult = ff4_calc_race_mult(attacker, target);
    ctx.target_count = 1;                        /* single-target attack */

    ctx.net_hits = ff4_phys_hits(rng, attacker, target,
                                 attacker_slot, target_slot);
    if (ctx.net_hits <= 0)
        return 0;                                /* miss: no damage */

    return ff4_calc_damage(rng, &ctx);
}

int ff4_magic_attack_power(int base_power, bool summon)
{
    /* @d060: summons x8, spells x4 */
    return summon ? base_power * 8 : base_power * 4;
}

/* ==================================================================== */
/*  Stats (UpdateCharStats @9837 + documented formulas)                 */
/* ==================================================================== */

const int8_t ff4_equip_stat_tbl[8][2] = {
    {  3,   0 },          /* +3/+0  */
    {  5,   0 },          /* +5/+0  */
    { 10,   0 },          /* +10/+0 */
    { 15,   0 },          /* +15/+0 */
    {  5,  -5 },          /* +5/-5  */
    { 10, -10 },          /* +10/-10 */
    { 15, -15 },          /* +15/-15 */
    {  5, -10 }           /* +5/-10 */
};

int ff4_equip_stat(const FF4_AssetDB *db, uint8_t item_id, FF4_StatId stat)
{
    const FF4_EquipDef *eq;
    int bit;

    if (!db || item_id > FF4_DB_EQUIP || (int)stat >= FF4_STAT_COUNT)
        return 0;
    eq = &db->equip[item_id];
    /* stat bits s(7) a(6) t(5) i(4) p(3); bonus index = low 3 bits */
    bit = 7 - (int)stat;
    return (eq->stats & (1 << bit))
               ? ff4_equip_stat_tbl[eq->stat_mod & 7][0]
               : ff4_equip_stat_tbl[eq->stat_mod & 7][1];
}

static uint8_t clamp_stat(int v)
{
    /* @98e1: >= 0xB6 -> 1 (unsigned 182..255 included - original bug),
       > 99 -> 99 */
    if (v >= 0xB6)
        return 1;
    if (v > 99)
        return 99;
    return (uint8_t)v;
}

/* weapon-type flag bits from EquipProp byte 6 (bateeeee). */
#define FF4_WPN_BOW    0x20    /* b: bow */
#define FF4_WPN_ARROW  0x40    /* a: arrows */

void ff4_calc_stats(const FF4_AssetDB *db, FF4_PartyMember *m)
{
    int i, s;
    int hands = 0;
    int arrow_hand = -1;
    uint8_t wpn_acc[2] = {0, 0}, wpn_pow[2] = {0, 0};
    bool bow = false;

    if (!db)
        return;

    /* ---- modified stats: base + EquipStatTbl rows over 5 slots ---- */
    {
        uint8_t modded[FF4_STAT_COUNT];

        modded[0] = m->base_str;
        modded[1] = m->base_agi;
        modded[2] = m->base_sta;
        modded[3] = m->base_int;
        modded[4] = m->base_spr;
        for (i = 0; i < FF4_EQ_SLOTS; ++i) {
            uint8_t id = m->equip[i];
            int s;

            if (!id || id > FF4_DB_EQUIP)
                continue;
            for (s = 0; s < FF4_STAT_COUNT; ++s)
                modded[s] = (uint8_t)(modded[s] + ff4_equip_stat(db, id,
                                                                 (FF4_StatId)s));
        }
        m->str  = clamp_stat(modded[0]);
        m->agi  = clamp_stat(modded[1]);
        m->sta  = clamp_stat(modded[2]);
        m->int_ = clamp_stat(modded[3]);
        m->spr  = clamp_stat(modded[4]);
    }

    /* ---- weapon configuration of the two hands ---- */
    for (i = 0; i < 2; ++i) {
        uint8_t id = m->equip[i];
        const FF4_EquipDef *eq;
        uint8_t wt;

        if (!id || id > FF4_DB_EQUIP)
            continue;
        eq = &db->equip[id];
        wt = eq->weapon_type;
        if (wt & FF4_WPN_ARROW) {
            arrow_hand = i;
            bow = true;                          /* arrows imply archer */
            continue;
        }
        if (wt & FF4_WPN_BOW) {
            bow = true;
            if (hands < 2) {
                wpn_acc[hands] = eq->attack_pct;
                wpn_pow[hands] = eq->attack;
                ++hands;
            }
            continue;
        }
        /* regular weapon */
        if (hands < 2) {
            wpn_acc[hands] = eq->attack_pct;
            wpn_pow[hands] = eq->attack;
            ++hands;
        }
        m->attack_element |= (uint8_t)(eq->element & FF4_ELEM_MASK);
        m->attack_status |= (uint16_t)((uint16_t)eq->status1 |
                                       ((uint16_t)eq->status2 << 8));
        m->attack_race |= eq->creature_type;
    }

    /* ---- derived attack block ($101B-$101F, docs formulas) ---- */
    m->attack_mult = (uint8_t)(m->str / 8 + m->agi / 16 + 1);

    {
        int lv4 = m->level / 4;

        if (bow) {
            uint8_t acc = 0, pow = 0;

            /* accuracy comes from the bow */
            for (i = 0; i < 2; ++i) {
                uint8_t id = m->equip[i];
                if (id && id <= FF4_DB_EQUIP &&
                    (db->equip[id].weapon_type & FF4_WPN_BOW)) {
                    acc = db->equip[id].attack_pct;
                    pow = db->equip[id].attack;
                }
            }
            m->hit_rate = (uint8_t)(acc + lv4);
            m->attack_power = (uint8_t)(pow / 2 + m->str / 4);
            if (arrow_hand >= 0 && m->equip[arrow_hand] <= FF4_DB_EQUIP)
                m->attack_power = (uint8_t)(m->attack_power +
                    db->equip[m->equip[arrow_hand]].attack);
            /* bow penalty: -1/5 */
            m->attack_power = (uint8_t)(m->attack_power -
                                        m->attack_power / 5);
            /* crit: bow+arrow x3 base, bonus + arrow power */
            m->crit_rate = (uint8_t)(m->crit_rate * 3);
            m->crit_bonus = (uint8_t)(m->crit_bonus +
                (arrow_hand >= 0 ? db->equip[m->equip[arrow_hand]].attack : 0));
            /* crit bonus is stored to a byte; 8-bit wrap matches the asm */
        } else if (hands == 0) {
            m->hit_rate = (uint8_t)(50 + lv4);
            m->attack_power = (uint8_t)(lv4 + m->str / 4);
        } else if (hands == 1) {
            m->hit_rate = (uint8_t)(wpn_acc[0] + lv4);
            m->attack_power = (uint8_t)(lv4 + m->str / 4 + wpn_pow[0]);
            m->crit_rate = (uint8_t)(m->crit_rate * 2);
            m->crit_bonus = (uint8_t)(m->crit_bonus + wpn_pow[0] / 2);
        } else {
            m->hit_rate = (uint8_t)((lv4 + lv4 + wpn_acc[0] + wpn_acc[1]) / 2);
            m->attack_power = (uint8_t)((lv4 + m->str / 4) * 2 +
                                        wpn_pow[0] + wpn_pow[1]);
        }
        if ((int)m->hit_rate > FF4_HIT_RATE_MAX)
            m->hit_rate = FF4_HIT_RATE_MAX;
        /* attack power wraps at 256 in the original 8-bit store; the
           documented cap is editorial - the byte behavior is the truth. */
        /* crit-rate cap quirk: single-weapon only caps when base >= 128 */
        if ((int)m->crit_rate >= 128 && (int)m->crit_rate > FF4_CRIT_RATE_MAX)
            m->crit_rate = FF4_CRIT_RATE_MAX;
    }

    /* Yang's barehand attack power override (Karate formula) */
    if (m->job == FF4_JOB_KARATE)
        m->attack_power = (uint8_t)(m->level * 2 + m->str / 4 + 2);

    /* ---- magic defence ($1022-$1024) ---- */
    m->mag_def_mult = (uint8_t)((m->int_ + m->spr) / 32 + m->agi / 32);
    i = (m->int_ + m->spr) / 8;
    for (s = 0; s < FF4_EQ_SLOTS; ++s) {
        uint8_t id = m->equip[s];

        if (!id || id > FF4_DB_EQUIP)
            continue;
        m->mag_def_rate = (uint8_t)(m->mag_def_rate +
                                    db->equip[id].magic_defense_pct);
        m->mag_def = (uint8_t)(m->mag_def + db->equip[id].magic_defense);
    }
    m->mag_def_rate = (uint8_t)(i + m->mag_def_rate);
    if ((int)m->mag_def_rate > 99)
        m->mag_def_rate = 99;
    /* mag defence accumulates in a byte and wraps, as in the asm */

    /* ---- physical defence ($1028-$102A) ---- */
    i = 0;
    for (s = 0; s < FF4_EQ_SLOTS; ++s) {
        uint8_t id = m->equip[s];

        if (id && id <= FF4_DB_EQUIP && db->equip[id].shield)
            ++i;                                 /* shields feed def mult */
    }
    m->def_mult = (uint8_t)((m->level / 16) * i + m->agi / 8);

    for (s = 0; s < FF4_EQ_SLOTS; ++s) {
        uint8_t id = m->equip[s];

        if (!id || id > FF4_DB_EQUIP)
            continue;
        m->def_rate = (uint8_t)(m->def_rate + db->equip[id].attack_pct);
        m->def = (uint8_t)(m->def + db->equip[id].attack);
    }
    /* empty body slot counts as 10 evade (documented quirk) */
    if (m->equip[FF4_EQ_BODY] == 0)
        m->def_rate = (uint8_t)(m->def_rate + 10);
    if ((int)m->def_rate > 99)
        m->def_rate = 99;
    m->def = (uint8_t)(m->def + m->sta / 2);
    /* (defence is a wrapped byte sum like magic defence) */

    /* ---- elemental resistances from equipment (all five slots) ---- */
    for (s = 0; s < FF4_EQ_SLOTS; ++s) {
        uint8_t id = m->equip[s];
        const FF4_EquipDef *eq;

        if (!id || id > FF4_DB_EQUIP)
            continue;
        eq = &db->equip[id];
        if (eq->element & 0x80)
            m->immune_elements |= (uint8_t)(eq->element & FF4_ELEM_MASK);
        else
            m->strong_elements |= (uint8_t)(eq->element & FF4_ELEM_MASK);
        m->race_defense |= eq->creature_type;
        m->status_immunity |= (uint16_t)((uint16_t)eq->status1 |
                                         ((uint16_t)eq->status2 << 8));
    }

    /* ---- weaknesses = opposites of resistances (equip.asm @9d9e):
           fire <-> ice, holy <-> darkness only ---- */
    {
        uint8_t s8 = m->strong_elements;
        uint8_t w = 0;

        if (s8 & FF4_ELEM_FIRE)   w |= FF4_ELEM_ICE;
        if (s8 & FF4_ELEM_ICE)    w |= FF4_ELEM_FIRE;
        if (s8 & FF4_ELEM_DARK)   w |= FF4_ELEM_HOLY;
        if (s8 & FF4_ELEM_HOLY)   w |= FF4_ELEM_DARK;
        m->weak_elements = w;

        /* very-weak = opposites of immunities ($1021) */
        s8 = m->immune_elements;
        w = 0;
        if (s8 & FF4_ELEM_FIRE)   w |= FF4_ELEM_ICE;
        if (s8 & FF4_ELEM_ICE)    w |= FF4_ELEM_FIRE;
        if (s8 & FF4_ELEM_DARK)   w |= FF4_ELEM_HOLY;
        if (s8 & FF4_ELEM_HOLY)   w |= FF4_ELEM_DARK;
        m->very_weak_elements = w;
    }
}

/* ==================================================================== */
/*  Statuses                                                            */
/* ==================================================================== */

bool ff4_inflict_status(FF4_PartyMember *target, uint8_t st1, uint8_t st2,
                        uint8_t st3, uint8_t st4, bool revive_flag)
{
    uint16_t immune = target->status_immunity;
    uint16_t incoming = (uint16_t)((uint16_t)st1 | ((uint16_t)st2 << 8));

    /* magic.asm @d53c: immunity bits AND-reject the incoming statuses */
    if ((incoming & immune) != 0)
        return false;

    /* dead/stone targets reject new statuses unless it is a revival
       (attack.asm @d004; Life/Esuna bypass lives in the effect code) */
    if (!revive_flag &&
        (target->status[0] & (FF4_ST1_DEAD | FF4_ST1_STONE)))
        return false;

    /* monsters under a new status clear charm/berserk (@d5e8) */
    if (target->creature_type != 0)
        target->status[1] &= (uint8_t)~(FF4_ST2_CHARM | FF4_ST2_BERSERK);

    /* the sleep/paralyze timer pair are mutually exclusive (@d5a2) */
    if (st2 & FF4_ST2_PARALYZE)
        target->status[1] &= (uint8_t)~FF4_ST2_SLEEP;
    if (st2 & FF4_ST2_SLEEP)
        target->status[1] &= (uint8_t)~FF4_ST2_PARALYZE;

    target->status[0] |= st1;
    target->status[1] |= st2;
    target->status[2] |= st3;
    target->status[3] |= st4;

    /* death bookkeeping mirrors UpdateDead (@b1a0): HP -> 0 and the
       non-dead-related statuses collapse */
    if (target->status[0] & FF4_ST1_DEAD) {
        target->hp = 0;
        target->status[0] &= (uint8_t)~FF4_ST1_POISON;
        target->status[1] &= (uint8_t)(FF4_ST2_CURSE | FF4_ST2_FLOAT);
        target->status[2] &= 0xA2;
        target->status[3] &= 0x80;
    }
    return true;
}