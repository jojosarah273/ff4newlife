/**
 * @file ff4_state.h
 * @brief Final Fantasy IV source port - native game state structures.
 *
 * Phase 1 deliverable. Every struct mirrors a region of the SNES WRAM layout
 * ($7E0000-$7E6CBF and SRAM save slots $700000-$701FFF) in *semantic* form:
 * fields are named by meaning, and the original RAM offset is cited in the
 * doc comment purely as traceability metadata. No code indexes by those
 * offsets; storage and serialization use the struct layout.
 *
 * Design notes
 * ------------
 *  - Bitmasks/enums come from ff4_types.h (statuses, elements, commands...).
 *  - In-battle character records ($2000: 5 * 128 bytes) are a strict superset
 *    of the field records ($1000: 5 * 64 bytes); FF4_PartyMember covers the
 *    union and is used for both, which keeps one type for the whole engine.
 *  - Monsters ($2280: 8 * 128 bytes) share the same layout as characters for
 *    bytes $00-$3F; FF4_Monster extends that common core with monster-only
 *    fields ($40 creature types, $51 command, $70 level/boss...).
 *  - ATB works on a 13-slot combatant space (5 characters + 8 monsters);
 *    FF4_BattleState owns the 13 timers, the action queue and the pending
 *    action rows, mirroring $29EA/$2A04, $3929 and $3938.
 */
#ifndef FF4_STATE_H
#define FF4_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "ff4_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Character / party member                                                 */
/* ========================================================================== */

/**
 * FF4_PartyMember - one character's full record.
 * Covers field records ($7E1000, 64b) and battle records ($7E2000, 128b).
 */
typedef struct FF4_PartyMember {
    /* --- identity / class ($1000-$1001) --- */
    FF4_CharacterId id;          /**< $1000 low 5 bits: character id. */
    FF4_JobClass job;            /**< $1001 low 4 bits: sprite/class. */
    bool right_handed;           /**< $1000 bit7. */
    bool left_handed;            /**< $1000 bit6. */
    bool back_row;               /**< $1001 bit7 (0 front / 1 back). */
    bool long_range;             /**< $1001 bit5: no row penalty (bug-prone). */

    /* --- progression ($1002, $1037, $103D) --- */
    uint8_t level;               /**< $1002. */
    uint32_t exp;                /**< $1037 (3 bytes, little-endian on disk). */
    uint32_t exp_last_level;     /**< $103D: exp threshold at last level-up. */

    /* --- HP / MP ($1007-$100E) --- */
    uint16_t hp;                 /**< $1007: current HP. */
    uint16_t max_hp;             /**< $1009. */
    uint16_t mp;                 /**< $100B: current MP. */
    uint16_t max_mp;             /**< $100D. */

    /* --- base stats ($100F-$1013) --- */
    uint8_t base_str;            /**< $100F strength. */
    uint8_t base_agi;            /**< $1010 agility. */
    uint8_t base_sta;            /**< $1011 stamina/vitality. */
    uint8_t base_int;            /**< $1012 intellect/wisdom. */
    uint8_t base_spr;            /**< $1013 spirit/will. */

    /* --- modified stats ($1014-$1018) --- */
    uint8_t str;                 /**< $1014: base + equipment, cap 99. */
    uint8_t agi;                 /**< $1015. */
    uint8_t sta;                 /**< $1016. */
    uint8_t int_;                /**< $1017. */
    uint8_t spr;                 /**< $1018. */

    /* --- attack properties ($1019-$101F) --- */
    uint8_t attack_element;      /**< $1019: union of weapon elements. */
    uint8_t attack_race;         /**< $101A: bonus damage vs creature types. */
    uint8_t attack_mult;         /**< $101B: str/8 + agi/16 + 1. */
    uint8_t hit_rate;            /**< $101C: accuracy, cap 99. */
    uint8_t attack_power;        /**< $101D: attack base, cap 255. */
    uint16_t attack_status;       /**< $101E-$101F: status carried by hits. */

    /* --- defence properties ($1020-$102C) --- */
    uint8_t weak_elements;       /**< $1020: elemental weaknesses. */
    uint8_t very_weak_elements;  /**< $1021: double-damage weaknesses. */
    uint8_t mag_def_mult;        /**< $1022: (int+spr)/32 + agi/32. */
    uint8_t mag_def_rate;        /**< $1023: magic evasion, cap 99. */
    uint8_t mag_def;             /**< $1024: magic defence, cap 255. */
    uint8_t strong_elements;     /**< $1025: elemental resistances. */
    uint8_t immune_elements;     /**< $1026: elemental immunities. */
    uint8_t race_defense;        /**< $1027: reduced damage vs races. */
    uint8_t def_mult;            /**< $1028: (lvl/16)*shields + agi/8. */
    uint8_t def_rate;            /**< $1029: physical evade, cap 99. */
    uint8_t def;                 /**< $102A: defence base, cap 255. */
    FF4_StatusImmunity status_immunity; /**< $102B-$102C (2 bytes). */

    /* --- criticals ($102D-$102E; battle copies at $2041-$2042) --- */
    uint8_t crit_rate;           /**< $102D (base); $2041 in battle. */
    uint8_t crit_bonus;          /**< $102E (base); $2042 in battle. */

    /* --- equipment ($1030-$1036) --- */
    uint8_t equip[FF4_EQ_SLOTS]; /**< head, body, finger, RH, LH (ids). */
    uint8_t right_qty;           /**< $1034: arrow stack in right hand. */
    uint8_t left_qty;            /**< $1036: arrow stack in left hand. */

    /* --- statuses ($1003-$1006) --- */
    uint8_t status[4];           /**< status bytes 1..4 (RAM $1003-$1006). */
    uint8_t speed_mod;           /**< $103B: ATB speed multiplier (16 = 100%). */

    /* --- battle-only extras ($2040-$2073) --- */
    uint8_t creature_type;       /**< $2040 (characters normally 0). */
    uint16_t base_timer;         /**< $2060: base ATB duration. */
    bool is_boss;                /**< $2070 bit7 (monsters). */
    uint8_t unknown_field;       /**< $103A (unused engine scratch). */
} FF4_PartyMember;

/* ========================================================================== */
/*  Spells known per character                                               */
/* ========================================================================== */

/** One tied slot of a character's magic lists ($1560: 13 lists * 24 bytes). */
typedef struct FF4_SpellSlot {
    FF4_SpellId id;              /**< Spell id (0 = empty). */
    uint8_t learned_level;       /**< Level at which it was learned. */
} FF4_SpellSlot;

/**
 * FF4_MagicLists - the three lists a character can hold
 * (white $1560, black $15A8 area, call $15C0 area; battle copies $2C7A).
 */
typedef struct FF4_MagicLists {
    FF4_SpellSlot white[FF4_SPELLS_PER_LIST];  /**< White magic. */
    FF4_SpellSlot black[FF4_SPELLS_PER_LIST];  /**< Black magic/ninjutsu. */
    FF4_SpellSlot summon[FF4_SPELLS_PER_LIST]; /**< Summon ("Call"). */
    uint8_t count[FF4_SPELL_LISTS_PER_CHAR];   /**< Slots in use per list. */
} FF4_MagicLists;

/* ========================================================================== */
/*  Inventory                                                                */
/* ========================================================================== */

/** One inventory entry (RAM $1440: 48 * 2 bytes; battle copy $321A). */
typedef struct FF4_InventorySlot {
    uint8_t item_id;             /**< FF4_ItemId. */
    uint8_t quantity;            /**< Stack size (0 = empty). */
} FF4_InventorySlot;

/**
 * FF4_Inventory - the 48-slot item bag plus game-progress key items
 * (key items occupy item ids $E7-$FA; presence tracked by individual slots
 * because FF4 tracks them like any item, but see key_items for a compact
 * bit view used by the UI).
 */
typedef struct FF4_Inventory {
    FF4_InventorySlot slots[FF4_INVENTORY_SLOTS]; /**< Item bag. */
    uint8_t count;               /**< Number of occupied slots (cached). */
    uint16_t gil;                /**< $16A0 (3-byte on disk). */
    uint32_t gil_full;           /**< Extended gil for >65535 handling. */
} FF4_Inventory;

/* ========================================================================== */
/*  World / field state                                                      */
/* ========================================================================== */

/**
 * FF4_WorldState - position, map, vehicles and story progress flags
 * ($1700-$17ED field block plus switch regions $1280-$131F).
 */
typedef struct FF4_WorldState {
    FF4_MapType map_type;        /**< $1700: overworld/underground/moon/sub. */
    FF4_World world;             /**< $1701: world id. */
    uint8_t map_id;              /**< $1702: current map id (see data/maps). */
    uint8_t showing_character;   /**< $1703: party leader index. */

    /* --- party position --- */
    uint16_t x;                  /**< $1706: tile x. */
    uint16_t y;                  /**< $1707: tile y. */
    FF4_Direction facing;        /**< $1705. */

    /* --- overworld vehicles --- */
    FF4_Vehicle active_vehicle;  /**< $1704: vehicle in use. */
    bool vehicle_visible[8];     /**< $170F-$172B: per-vehicle visibility. */
    uint16_t vehicle_pos[8][2];  /**< $1710+ : per-vehicle x/y in tiles. */
    uint8_t vehicle_world[8];    /**< $171B+ : per-vehicle world. */

    /* --- sub-map return stack ($172C-$17ED, 64 * 3-entry ring) --- */
    uint8_t prev_map[64][3];     /**< map id, dir/pos byte, y. */
    uint8_t prev_map_count;      /**< Stack depth. */

    /* --- switches ($1280-$131F) --- */
    uint8_t event_switches[32];  /**< $1280: 256 event flags (bit field). */
    uint8_t treasure_switches[64]; /**< $12A0: 512 treasure-opened flags. */
    uint8_t npc_switches[64];    /**< $12E0: 512 npc/story flags. */

    /* --- misc field controls --- */
    bool battle_enabled;         /**< $85: permit random battles here. */
    bool player_control;         /**< $D5: player may move. */
    bool event_running;          /**< $B1: event script active. */
    bool hide_player;            /**< $D3: hide player sprite. */
    bool spin_move;              /**< $D8: player spins while walking. */
    FF4_ZLevel z;                /**< $D2 z-level bits. */
    uint16_t frame_counter;      /**< $0FFF: global frame counter (RNG seed). */
} FF4_WorldState;

/* ========================================================================== */
/*  Battle state                                                            */
/* ========================================================================== */

/** One battle timer slot ($2A04: 13 slots * 3 bytes, +flag byte). */
typedef struct FF4_Timer {
    uint16_t value;              /**< Frames remaining. */
    uint8_t flags;               /**< st--a--p: stopped/effect/action/pending. */
    FF4_TimerType type;          /**< Timer kind driving duration. */
} FF4_Timer;

/** Enabled-timer bitmask ($29EB, s stop / m action / a sap / p poison / e
 *  petrify / w reflect / d doom). */
typedef enum FF4_TimerEnableBit {
    FF4_TIMER_EN_DOOM = 0x01,    /**< d. */
    FF4_TIMER_EN_REFLECT = 0x02, /**< w. */
    FF4_TIMER_EN_PETRIFY = 0x04, /**< e. */
    FF4_TIMER_EN_POISON = 0x08,  /**< p. */
    FF4_TIMER_EN_SAP = 0x10,     /**< a. */
    FF4_TIMER_EN_ACTION = 0x20,  /**< m (ATB). */
    FF4_TIMER_EN_STOP = 0x40     /**< s. */
} FF4_TimerEnableBit;

/**
 * Combatant id: character slots 0-4, monster slots 5-12 ($CD/$CE/... keep
 * the monster high bit in the *raw* exchange ids used by the engine).
 */
typedef enum FF4_CombatantId {
    FF4_COMBAT_CHAR0 = 0,
    FF4_COMBAT_CHAR1 = 1,
    FF4_COMBAT_CHAR2 = 2,
    FF4_COMBAT_CHAR3 = 3,
    FF4_COMBAT_CHAR4 = 4,
    FF4_COMBAT_MON0 = 5,
    FF4_COMBAT_MON1 = 6,
    FF4_COMBAT_MON2 = 7,
    FF4_COMBAT_MON3 = 8,
    FF4_COMBAT_MON4 = 9,
    FF4_COMBAT_MON5 = 10,
    FF4_COMBAT_MON6 = 11,
    FF4_COMBAT_MON7 = 12,
    FF4_COMBAT_NONE = 0xFF
} FF4_CombatantId;

/** Pending action row (RAW $D1/$D2/$DC/$2051/$2052 + target masks). */
typedef struct FF4_PendingAction {
    FF4_CombatantId actor;       /**< Who acts ($D2 / $352F). */
    FF4_BattleCommand command;   /**< Chosen command ($2051). */
    FF4_ItemId item;             /**< Item used ($2052, if command = Item). */
    FF4_SpellId spell;           /**< Spell cast (attack id form in data). */
    uint8_t target_mask_chars;   /**< $2054: target character bitmask. */
    uint8_t target_mask_mons;    /**< $2053: target monster bitmask. */
    bool target_self;            /**< $3553 self-target flag. */
} FF4_PendingAction;

/** Monster battle record ($2280: 8 * 128 bytes; superset of char core). */
typedef struct FF4_Monster {
    FF4_PartyMember core;        /**< Shared $00-$3F layout (hp, stats...). */

    /* Monster-only extensions ($40+). */
    uint8_t command_used;        /**< $2051+$40: command on this turn. */
    uint16_t base_timer_dur;     /**< $2060: ATB duration. */
    uint8_t level_boss;          /**< $2070: bit7 boss, low 7 level. */
    uint8_t item_drop[2];        /**< $2073: rate + item set id. */
    uint8_t position_x;          /**< $29A5 area: formation slot x. */
    uint8_t position_y;          /**< Formation slot y. */
    bool hidden;                 /**< $29A0: not yet appeared. */
    uint8_t condition_idx;       /**< $35C7: AI condition script id. */
} FF4_Monster;

/**
 * FF4_BattleState - one battle session ($1800-$18FF, $2000-$35FF regions).
 */
typedef struct FF4_BattleState {
    bool active;                 /**< Battle is in progress. */
    uint8_t formation_id;        /**< Current formation (battleProperties id). */
    int last_actor;              /**< $38f6 round-robin cursor. */

    /* --- formation ($299C-$29B4) --- */
    uint8_t monster_types[FF4_MONSTER_TYPES_MAX]; /**< $299D-$299F ids. */
    uint8_t monster_counts[FF4_MONSTER_TYPES_MAX];/**< $29A0 nibbles. */
    uint8_t total_monsters;      /**< $29CD. */
    uint8_t battle_bg;           /**< $1802 low nibble: bg id. */
    bool has_magnetize;          /**< $1802 bit7 (Cave Magnes). */
    bool can_escape;             /**< $29A2 bit0. */
    bool no_game_over;           /**< $29A2 bit1: defeat is not fatal. */
    bool monsters_first;         /**< $38D7: monster ambush. */
    bool chars_first;            /**< $38D7: pre-emptive strike. */
    uint8_t battle_music;        /**< $29A2 bits: BGM selection. */

    /* --- combatants --- */
    FF4_PartyMember party[FF4_PARTY_MAX];   /**< Battle copies ($2000). */
    FF4_Monster monsters[FF4_MONSTER_SLOTS_MAX]; /**< $2280. */
    uint8_t slot_map[FF4_COMBATANT_SLOTS_MAX];  /**< $29C5: char ids present. */
    bool present[FF4_COMBATANT_SLOTS_MAX];      /**< $3540: operand aliveness. */

    /* --- ATB / timers ($29EA-$2B14) --- */
    FF4_Timer timers[FF4_TIMER_SLOTS];  /**< $2A04: 13 slots. */
    uint16_t timer_enabled;             /**< $29EB bits: enabled timers. */
    uint16_t atb_duration[FF4_COMBATANT_SLOTS_MAX]; /**< ATB gauge caps. */
    uint8_t poison_pend[FF4_COMBATANT_SLOTS_MAX];   /**< Poison tick counter. */
    uint8_t rng_table[256];             /**< RNG sequence (battle copy). */
    uint8_t rng_ptr;                    /**< RNG index ($97). */
    FF4_TimerType atb_timer_type;       /**< $D6: current feed kind. */
    uint16_t battle_speed_counter;      /**< $3538/$38EE: frames per tick. */

    /* --- action queue ($3929-$393C + $35FF/$3600) --- */
    FF4_PendingAction queue[5];         /**< Pending character actions. */
    uint8_t queue_size;                 /**< $392F. */
    uint8_t current_actor;              /**< $D2: acting combatant. */
    uint8_t battle_command_used;        /**< $35FF: previous command. */
    uint8_t attack_element;             /**< $3600: current attack element. */

    /* --- damage exchange ($34D4-$3521) --- */
    int16_t damage[FF4_COMBATANT_SLOTS_MAX]; /**< $34D4: per-slot deltas. */
    uint8_t damage_flags[FF4_COMBATANT_SLOTS_MAX]; /**< $34D4 msb: miss/restore. */

    /* --- rewards ($3591-$3597) --- */
    uint32_t exp_gained;            /**< $3591 (3 bytes). */
    uint32_t gil_gained;            /**< $3594 (3 bytes). */
    uint32_t formation_exp;         /**< Sum of the formation's exp. */
    uint32_t formation_gil;         /**< Sum of the formation's gil. */
    uint8_t item_gained[8];         /**< $1804: drops this battle. */

    /* --- results --- */
    FF4_BattleEndFlags end_flags;   /**< $1803. */
    uint8_t turns_elapsed;          /**< Battle turn counter. */
} FF4_BattleState;

/* ========================================================================== */
/*  System settings + top-level game state                                   */
/* ========================================================================== */

/**
 * FF4_SystemSettings - persisted config block ($16A3-$16BE).
 */
typedef struct FF4_SystemSettings {
    uint32_t playtime_frames;   /**< $16A3: frame counter + $16A4 game time. */
    FF4_RowSetting row_setting; /**< $16A8. */
    uint8_t window_color[3];    /**< $1BA8: RGB components. */
    uint8_t battle_speed;       /**< $16AC (0..5). */
    FF4_TextSpeed text_speed;   /**< $16AD. */
    FF4_BattleMode battle_mode; /**< $16BE. */
    FF4_ControllerMode controller_mode; /**< $16B8. */
    bool mono_audio;            /**< $16B6: 0 stereo / 1 mono. */
    uint8_t confirm_button;     /**< $1A37..$1A3B mapping summary. */
    uint8_t button_map[5];      /**< $1A05-$1A1C: default + custom maps. */
} FF4_SystemSettings;

/**
 * FF4_GameState - the whole game.
 */
typedef struct FF4_GameState {
    FF4_PartyMember party[FF4_PARTY_MAX];   /**< Field copies ($1000). */
    FF4_MagicLists magic[FF4_PARTY_MAX];    /**< Spell lists ($1560). */
    FF4_Inventory inventory;                /**< Item bag ($1440). */
    FF4_WorldState world;                   /**< Field ($1700). */
    FF4_BattleState battle;                 /**< Battle ($2000). */
    FF4_SystemSettings settings;            /**< Config ($16A3). */

    /* --- global runtime bookkeeping --- */
    bool save_valid;                        /**< $17FB: slot loadable. */
    uint8_t save_slot;                      /**< Active slot (1..4). */
    uint16_t sram_checksum;                 /**< $17FC: sum of $1000-$17FA. */

    /* --- story/runtime tables --- */
    FF4_PartyMember saved_char[5];          /**< $1140: 5 saved member copies
                                                  (Kain/Tellah/Rosa/Yang/Rydia
                                                  slots) used by party events. */
    uint8_t spell_tables[FF4_SPELL_LIST_COUNT][FF4_SPELLS_PER_LIST]; /**< $1560
                                                  shared 13x24 spell lists. */
} FF4_GameState;

/* ========================================================================== */
/*  State API surface (implemented in src/core/ff4_state.c)                  */
/* ========================================================================== */

void ff4_state_init(FF4_GameState *g);
void ff4_state_reset_world(FF4_WorldState *w);
void ff4_party_add(FF4_GameState *g, FF4_CharacterId id, uint8_t slot);
void ff4_party_remove(FF4_GameState *g, uint8_t slot);
FF4_PartyMember *ff4_party_member(FF4_GameState *g, uint8_t slot);

bool ff4_status_has(const uint8_t status[4], uint8_t byte, uint8_t mask);
void ff4_status_set(uint8_t status[4], uint8_t byte, uint8_t mask);
void ff4_status_clear(uint8_t status[4], uint8_t byte, uint8_t mask);

int ff4_inventory_find(const FF4_Inventory *inv, FF4_ItemId id);
bool ff4_inventory_add(FF4_Inventory *inv, FF4_ItemId id, uint8_t qty);
bool ff4_inventory_remove(FF4_Inventory *inv, FF4_ItemId id, uint8_t qty);
bool ff4_inventory_has_item(const FF4_Inventory *inv, FF4_ItemId id);
uint8_t ff4_inventory_count(const FF4_Inventory *inv);

void ff4_battle_init(FF4_BattleState *b);
void ff4_battle_tick(FF4_BattleState *b, uint16_t frames);
void ff4_battle_queue_action(FF4_BattleState *b, const FF4_PendingAction *a);

/** Negotiate level-up for one member, returning number of levels gained. */
uint8_t ff4_exp_apply(FF4_PartyMember *m, uint32_t exp_gain);

#ifdef __cplusplus
}
#endif

#endif /* FF4_STATE_H */