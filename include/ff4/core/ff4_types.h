/**
 * @file ff4_types.h
 * @brief Final Fantasy IV source port - fundamental types, enums and constants.
 *
 * Phase 1 deliverable. All constants are grounded in the authoritative
 * references, cross-verified against the live rip data:
 *
 *   - everything8215/ff4  (disassembly + `make rip` output, ff4-en-data.json)
 *   - aexoden/ff4-documentation (data-structures.rst, RAM/ROM maps)
 *
 * Conventions
 * -----------
 *  - Explicit <stdint.h> widths are used throughout; no register-emulating
 *    types (no uint16_t A / SEP-REP wrappers).
 *  - Bit layouts mirror the SNES WRAM bytes 1:1 so a future save-file
 *    serializer can round-trip the original format without transform logic.
 *  - RAM offsets of the original game are cited in doc comments for
 *    traceability; they are reference metadata only, never used as indices.
 *    All *game logic* indexes data by semantic id (e.g. item id), and the
 *    id -> ROM/JSON-row mapping lives in the Phase 2 asset loader.
 *  - Bits that exist in the Japanese build but were stripped/renamed in the
 *    US release are present here and tagged "(JP)".
 */
#ifndef FF4_TYPES_H
#define FF4_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Global platform-neutral configuration constants                           */
/* ========================================================================== */

/** Maximum party size (field + battle), SNES WRAM $1000, 5 * 64-byte records. */
#define FF4_PARTY_MAX 5

/** Maximum active enemy slots in battle (battle RAM $2280, 8 * 128 bytes). */
#define FF4_MONSTER_SLOTS_MAX 8

/** Combined combatant id space: 5 characters + 8 monsters (timers/damage). */
#define FF4_COMBATANT_SLOTS_MAX (FF4_PARTY_MAX + FF4_MONSTER_SLOTS_MAX)

/** Inventory capacity (RAM $1440, 48 * 2-byte entries; battle copy $321A). */
#define FF4_INVENTORY_SLOTS 48

/** Fat Chocobo item deposit cap (RAM $1340-$143F, 64 entries). */
#define FF4_FAT_CHOCOBO_SLOTS 64

/** Spells per magic list (RAM $1560: 13 lists * 24 bytes). */
#define FF4_SPELLS_PER_LIST 24

/** Magic lists per character (field), battle copy is 3 * 24 4-byte rows. */
#define FF4_SPELL_LISTS_PER_CHAR 3

/** Number of distinct magic lists in the game (RAM $1560-$1697). */
#define FF4_SPELL_LIST_COUNT 13

/** Size of the global item-name/id space (ROM: 256 * 9-byte names). */
#define FF4_ITEM_TABLE_SIZE 256

/** Size of the spell table (ROM: 72 * 6-byte names, ids $00-$47). */
#define FF4_SPELL_TABLE_SIZE 72

/** Size of the attack/property table (ROM: 256 * 6 bytes). */
#define FF4_ATTACK_TABLE_SIZE 256

/** Monster property table size (ROM: 224 * 128-byte battle records). */
#define FF4_MONSTER_TABLE_SIZE 224

/** Weapon/armour property table size (ROM: 176 * 8 bytes, ids $00-$AF). */
#define FF4_EQUIP_TABLE_SIZE 176

/** Consumable item property table size (ROM: 48 * 6 bytes, ids $B0-$DF). */
#define FF4_CONSUMABLE_TABLE_SIZE 48

/** Maximum distinct monster types in one random encounter (max 3). */
#define FF4_MONSTER_TYPES_MAX 3

/** Battle timer slots (RAM $29EA/$2A04, 13 slots). */
#define FF4_TIMER_SLOTS 13

/** Map title entries (ROM mapTitle: 121 entries). */
#define FF4_MAP_TITLE_COUNT 121

/** Character battle-command slots per character (RAM $3302, 7 * 4 bytes). */
#define FF4_COMMANDS_PER_CHAR 7

/** Equipment slots a character can wear. */
#define FF4_EQ_SLOTS 5

/** Core stat count (STR/AGI/STA/INT/SPR). */
#define FF4_STAT_COUNT 5

/** Numeric caps (from data-structures.rst calculations). */
#define FF4_LEVEL_MAX 99        /**< Character level $1002. */
#define FF4_STAT_CAP 99         /**< Derived stat upper bound, e.g. STR $14. */
#define FF4_ATTACK_POWER_MAX 255/**< Attack power cap ($1D). */
#define FF4_DEFENSE_MAX 255     /**< Physical/magical defense cap ($24/$2A). */
#define FF4_HIT_RATE_MAX 99     /**< Hit/magic-defence rate cap. */
#define FF4_CRIT_RATE_MAX 99    /**< Critical rate cap ($2D). */
#define FF4_HP_MAX 9999         /**< Display ceiling for HP. */
#define FF4_MP_MAX 999          /**< Display ceiling for MP. */
#define FF4_GIL_MAX 9999999     /**< Gil ceiling (3-byte accumulator). */
#define FF4_ITEM_QTY_MAX 99     /**< Stack quantity ceiling. */

/* ========================================================================== */
/*  Character IDs ($1000 char byte 1, low 5 bits)                             */
/* ========================================================================== */

typedef enum FF4_CharacterId {
    FF4_CHAR_NONE = 0x00,        /**< Empty slot. */
    FF4_CHAR_CECIL_DK = 0x01,    /**< Cecil, dark knight (until Baron -> Mist). */
    FF4_CHAR_KAIN_1 = 0x02,      /**< Kain (1st sting, until Mt. Ordeals). */
    FF4_CHAR_RYDIA_CHILD = 0x03, /**< Rydia (child). */
    FF4_CHAR_TELLAH_1 = 0x04,    /**< Tellah (until Damcyan). */
    FF4_CHAR_EDWARD = 0x05,      /**< Edward. */
    FF4_CHAR_ROSA_1 = 0x06,      /**< Rosa (1st stint). */
    FF4_CHAR_YANG_1 = 0x07,      /**< Yang (1st stint). */
    FF4_CHAR_PALOM = 0x08,       /**< Palom. */
    FF4_CHAR_POROM = 0x09,       /**< Porom. */
    FF4_CHAR_TELLAH_2 = 0x0A,    /**< Tellah (2nd stint, Damcyan -> Fabul). */
    FF4_CHAR_CECIL_PALADIN = 0x0B, /**< Cecil (paladin). */
    FF4_CHAR_TELLAH_3 = 0x0C,    /**< Tellah (3rd stint, until Tower of Zot). */
    FF4_CHAR_YANG_2 = 0x0D,      /**< Yang (2nd stint, until Super Cannon). */
    FF4_CHAR_CID = 0x0E,         /**< Cid. */
    FF4_CHAR_KAIN_2 = 0x0F,      /**< Kain (2nd stint, until Sealed Cave). */
    FF4_CHAR_ROSA_2 = 0x10,      /**< Rosa (final). */
    FF4_CHAR_RYDIA_ADULT = 0x11, /**< Rydia (adult). */
    FF4_CHAR_EDGE = 0x12,        /**< Edge. */
    FF4_CHAR_FUSOYA = 0x13,      /**< FuSoYa. */
    FF4_CHAR_KAIN_3 = 0x14,      /**< Kain (final, post-Sealed Cave). */
    FF4_CHAR_GOLBEZ = 0x15,      /**< Golbez (NPC/boss-controlled). */
    FF4_CHAR_ANNA = 0x16         /**< Anna (event-only). */
} FF4_CharacterId;

#define FF4_CHARACTER_ID_COUNT 0x17 /**< 23 ids incl. FF4_CHAR_NONE. */

/* ========================================================================== */
/*  Job / class ids ($1001 low 4 bits; sprite tied to job)                    */
/* ========================================================================== */

typedef enum FF4_JobClass {
    FF4_JOB_DKNIGHT = 0x00,  /**< Dark Knight (Cecil). */
    FF4_JOB_DRAGOON = 0x01,  /**< Dragoon (Kain). */
    FF4_JOB_CALLER_CHILD = 0x02, /**< Summoner (Rydia child). */
    FF4_JOB_SAGE = 0x03,     /**< Sage (Tellah). */
    FF4_JOB_BARD = 0x04,     /**< Bard (Edward). */
    FF4_JOB_WHITE_WIZARD = 0x05, /**< White Mage (Rosa). */
    FF4_JOB_KARATE = 0x06,   /**< Monk (Yang). */
    FF4_JOB_BLACK_WIZARD = 0x07, /**< Black Mage (Palom). */
    FF4_JOB_WHITE_WIZARD_2 = 0x08, /**< White Mage (Porom). */
    FF4_JOB_PALADIN = 0x09,  /**< Paladin (Cecil post-Ordeals). */
    FF4_JOB_CHIEF = 0x0A,    /**< Engineer (Cid). */
    FF4_JOB_CALLER_ADULT = 0x0B, /**< Summoner (Rydia adult). */
    FF4_JOB_NINJA = 0x0C,    /**< Ninja (Edge). */
    FF4_JOB_LUNAR = 0x0D,    /**< Lunarian (FuSoYa). */
    FF4_JOB_PIG = 0x0E,      /**< Garbage pig data (JP). */
    FF4_JOB_GOLBEZ = 0x0F    /**< Golbez battle sprite (JP). */
} FF4_JobClass;

/**
 * Battle-row and handedness bits of $1000/$1001.
 * $1000: bit7 = right-hand weapon usable, bit6 = left-hand usable.
 * $1001: bit7 = back row, bit5 = no back-row penalty ("long range").
 */
typedef enum FF4_CharByte0Bits {
    FF4_CHAR_LEFT_HANDED = 0x40,  /**< Bit6 $1000: can equip left-hand weapon. */
    FF4_CHAR_RIGHT_HANDED = 0x80, /**< Bit7 $1000: can equip right-hand weapon. */
    FF4_CHAR_BACK_ROW = 0x80,     /**< Bit7 $1001: battle row (0 front/1 back). */
    FF4_CHAR_NO_ROW_PENALTY = 0x20 /**< Bit5 $1001: damage unaffected by row. */
} FF4_CharByte0Bits;

/* ========================================================================== */
/*  Battle rows                                                               */
/* ========================================================================== */

typedef enum FF4_Row {
    FF4_ROW_FRONT = 0,
    FF4_ROW_BACK = 1
} FF4_Row;

/* ========================================================================== */
/*  Status effects - 4 status bytes ($1003-$1006)                             */
/*  Byte $1003 is "status 1", $1004 "status 2", $1005 "status 3", $1006      */
/*  "status 4". Bit layouts from RAM map + data-structures.rst. Every bit     */
/*  (including JP-only/internal ones) is exposed.                             */
/* ========================================================================== */

typedef enum FF4_StatusByte1 {
    FF4_ST1_POISON = 0x01,   /**< bit0: Poison. */
    FF4_ST1_BLIND = 0x02,    /**< bit1: Darkness/Blind. */
    FF4_ST1_MUTE = 0x04,     /**< bit2: Mute/Silence. */
    FF4_ST1_PIG = 0x08,      /**< bit3: Pig. */
    FF4_ST1_MINI = 0x10,     /**< bit4: Small/Mini. */
    FF4_ST1_TOAD = 0x20,     /**< bit5: Toad. */
    FF4_ST1_STONE = 0x40,    /**< bit6: Stone (fully petrified). */
    FF4_ST1_DEAD = 0x80     /**< bit7: Swoon/Death. */
} FF4_StatusByte1;

typedef enum FF4_StatusByte2 {
    FF4_ST2_PETRIFY_1_3 = 0x01, /**< bit0: gradual petrification (1/3). */
    FF4_ST2_PETRIFY_2_3 = 0x02, /**< bit1: gradual petrification (2/3). */
    FF4_ST2_BERSERK = 0x04,     /**< bit2: Berserk. */
    FF4_ST2_CHARM = 0x08,       /**< bit3: Charm/Confuse. */
    FF4_ST2_SLEEP = 0x10,       /**< bit4: Sleep. */
    FF4_ST2_PARALYZE = 0x20,    /**< bit5: Paralysis/Paralyze. */
    FF4_ST2_FLOAT = 0x40,       /**< bit6: Float (over hazard tiles). */
    FF4_ST2_CURSE = 0x80        /**< bit7: Curse. */
} FF4_StatusByte2;

typedef enum FF4_StatusByte3 {
    FF4_ST3_DOOM = 0x01,       /**< bit0: Doom/Countdown. */
    FF4_ST3_JUMP = 0x02,       /**< bit1: Kain mid-Jump. */
    FF4_ST3_TWIN = 0x04,       /**< bit2: Twin-cast active (Palom/Porom). */
    FF4_ST3_FOCUS = 0x08,      /**< bit3: Yang Focus-charging. */
    FF4_ST3_DEFEND = 0x10,     /**< bit4: Defending this round. */
    FF4_ST3_EGG = 0x20,        /**< bit5: (JP) Egg (monster egg holding). */
    FF4_ST3_STOP = 0x40,       /**< bit6: Stop. */
    FF4_ST3_MAGNETIZED = 0x80  /**< bit7: Magnetized (Cave Magnes). */
} FF4_StatusByte3;

typedef enum FF4_StatusByte4 {
    FF4_ST4_LOW_HP = 0x01,    /**< bit0: HP critical (auto-cover detection). */
    FF4_ST4_COVER = 0x02,     /**< bit1: Cecil Cover-protecting a target. */
    FF4_ST4_IMAGE_1 = 0x04,   /**< bit2: Image (1 hit remaining). */
    FF4_ST4_IMAGE_2 = 0x08,   /**< bit3: Image (2 hits remaining). */
    FF4_ST4_BARRIER = 0x10,   /**< bit4: Barrier (halves physical damage). */
    FF4_ST4_REFLECT = 0x20,   /**< bit5: Wall/Reflect. */
    FF4_ST4_HP_LEAK = 0x40,   /**< bit6: HP leak (Sap/Regen timer driver). */
    FF4_ST4_HIDE = 0x80       /**< bit7: Hidden (Edward's Hide). */
} FF4_StatusByte4;

/** Raw 32-bit status encoding (status1<<24 | status2<<16 | status3<<8 | s4). */
typedef uint32_t FF4_StatusFlags;

#define FF4_STATUS_PACK(byte1, byte2, byte3, byte4) \
    (((uint32_t)(byte1) << 24) | ((uint32_t)(byte2) << 16) | \
     ((uint32_t)(byte3) << 8) | (uint32_t)(byte4))

#define FF4_STATUS_GET_1(f) ((uint8_t)(((f) >> 24) & 0xFF))
#define FF4_STATUS_GET_2(f) ((uint8_t)(((f) >> 16) & 0xFF))
#define FF4_STATUS_GET_3(f) ((uint8_t)(((f) >> 8) & 0xFF))
#define FF4_STATUS_GET_4(f) ((uint8_t)((f) & 0xFF))

/**
 * Status immunity mask ($102B-$102C). Statuses a character's equipment
 * grants immunity to; only the 16 attack-inflictable bits of bytes 1/2.
 */
typedef uint16_t FF4_StatusImmunity;

/* ========================================================================== */
/*  Elements ($1019 attack element; $1020 weak/$1021 very weak,               */
/*  $1025 strong, $1026 immune). Verified against EquipProp + monster data:   */
/*  FireClaw=0x01, IceClaw=0x02, ThunderClaw=0x04 and Treant (fire-weak)      */
/*  =0x01, undead (holy+fire) =0x11, water kin (lightning-weak) =0x04.       */
/* ========================================================================== */

typedef enum FF4_Element {
    FF4_ELEM_FIRE = 0x01,     /**< bit0: Fire. */
    FF4_ELEM_ICE = 0x02,      /**< bit1: Ice. */
    FF4_ELEM_THUNDER = 0x04,  /**< bit2: Lightning. */
    FF4_ELEM_DARK = 0x08,     /**< bit3: Darkness. */
    FF4_ELEM_HOLY = 0x10,     /**< bit4: Holy. */

    /* Bits 5-7 in the *weakness* bytes have non-elemental meaning
       (RAM map $1020): 0x20 = spear/arrow effectiveness vs flying,
       0x40 = drain, 0x80 = "very weak" wildcard. Kept out of the
       element mask; see FF4_WeaknessFlags below. */
    FF4_ELEM_MASK = 0x1F      /**< Valid elemental bits only. */
} FF4_Element;

typedef enum FF4_WeaknessFlags {
    FF4_WEAK_FLYING_VULN = 0x20, /**< bit5: vulnerable to spear/arrow (flying). */
    FF4_WEAK_DRAIN = 0x40,       /**< bit6: vulnerable to HP drain. */
    FF4_WEAK_ANY = 0x80          /**< bit7: "very weak" (all elements hit harder). */
} FF4_WeaknessFlags;

/* ========================================================================== */
/*  Creature races ($2040 creature types byte; also monster property field)   */
/* ========================================================================== */

typedef enum FF4_CreatureType {
    FF4_CREATURE_DRAGON = 0x01,  /**< bit0: Dragon. */
    FF4_CREATURE_MACHINE = 0x02, /**< bit1: Machine. */
    FF4_CREATURE_REPTILE = 0x04, /**< bit2: Reptile. */
    FF4_CREATURE_SPIRIT = 0x08,  /**< bit3: Spirit. */
    FF4_CREATURE_GIANT = 0x10,   /**< bit4: Giant. */
    FF4_CREATURE_SLIME = 0x20,   /**< bit5: Slime. */
    FF4_CREATURE_MAGE = 0x40,    /**< bit6: Mage. */
    FF4_CREATURE_UNDEAD = 0x80   /**< bit7: Undead. */
} FF4_CreatureType;

/* ========================================================================== */
/*  Battle commands ($2051 battle command used; 0x00-0x24).                   */
/*  Names are the JP engine semantics (RAM map) with US display names in      */
/*  comments. US-stripped commands are kept - tagged (JP).                    */
/* ========================================================================== */

typedef enum FF4_BattleCommand {
    FF4_CMD_FIGHT = 0x00,     /**< Fight. */
    FF4_CMD_ITEM = 0x01,      /**< Item. */
    FF4_CMD_WHITE = 0x02,     /**< White magic. */
    FF4_CMD_BLACK = 0x03,     /**< Black magic. */
    FF4_CMD_CALL = 0x04,      /**< Summon ("Call"). */
    FF4_CMD_DARK = 0x05,      /**< (JP) Cecil's Dark; stripped in US. */
    FF4_CMD_JUMP = 0x06,      /**< Jump (Kain). */
    FF4_CMD_RECALL = 0x07,    /**< (JP) Recall - bring Kain down early. */
    FF4_CMD_SING = 0x08,      /**< Sing (Edward). */
    FF4_CMD_HIDE = 0x09,      /**< Hide (Edward). */
    FF4_CMD_SALVE = 0x0A,     /**< (JP) Salve (auto-heal item toss). */
    FF4_CMD_PRAY = 0x0B,      /**< (JP) Pray (Rosa group heal). */
    FF4_CMD_AIM = 0x0C,       /**< Aim ("Cry"? US "Aim"). */
    FF4_CMD_FOCUS = 0x0D,     /**< (JP) Yang's Focus/Charge. */
    FF4_CMD_KICK = 0x0E,      /**< Kick (Yang). */
    FF4_CMD_BRACE = 0x0F,     /**< (JP) Yang's Brace (stat-halving skill). */
    FF4_CMD_TWIN = 0x10,      /**< Twin (Palom/Porom twincast). */
    FF4_CMD_BLUFF = 0x11,     /**< (JP) Edge's Bluff. */
    FF4_CMD_CRY = 0x12,       /**< (JP) Edge's Cry. */
    FF4_CMD_COVER = 0x13,     /**< Cover (Cecil). */
    FF4_CMD_PEEP = 0x14,      /**< Peep/Search (Cid, US "Peep"). */
    FF4_CMD_AIRSHIP = 0x15,   /**< Unused engine command. */
    FF4_CMD_DART = 0x16,      /**< Dart/Throw (Edge, US "Dart"). */
    FF4_CMD_SNEAK = 0x17,     /**< Sneak/Steal (Edge, US "Sneak"). */
    FF4_CMD_NINJUTSU = 0x18,  /**< Ninjutsu (Edge). */
    FF4_CMD_REGEN = 0x19,     /**< (JP) Regen (Rosa/FuSoYa bind). */
    FF4_CMD_CHANGE_ROW = 0x1A,/**< Change row (US "Chang"). */
    FF4_CMD_DEFEND = 0x1B,    /**< Defend (US "Parry"). */
    FF4_CMD_SHOW = 0x1C,      /**< Appear/Show (revert Hide). */
    FF4_CMD_COVER_OFF = 0x1D, /**< Disable Cover (US "Off"). */
    FF4_CMD_JUMP_2 = 0x1E,    /**< (JP) Jump phase 2 (engine internal). */
    FF4_CMD_FOCUS_2 = 0x1F,   /**< (JP) Focus phase 2. */
    FF4_CMD_TWIN_2 = 0x20,    /**< (JP) Twin phase 2. */
    FF4_CMD_DO_NOTHING = 0x21,/**< Do nothing. */
    FF4_CMD_END_REGEN = 0x22, /**< (JP) End Regen. */
    FF4_CMD_UNUSED_23 = 0x23, /**< Unused. */
    FF4_CMD_RUN = 0x24        /**< Run away. */
} FF4_BattleCommand;

#define FF4_BATTLE_COMMAND_COUNT 0x25 /**< 37 command ids. */

/** Command targeting flags (RAM $3302 + CmdTargetTbl). */
typedef enum FF4_CmdTargetFlags {
    FF4_CmdT_SELF = 0x08,      /**< f: must target self. */
    FF4_CmdT_SELECTABLE = 0x10,/**< s: target selectable. */
    FF4_CmdT_ALL = 0x20,       /**< a: can target all. */
    FF4_CmdT_ENEMY_DEFAULT = 0x40, /**< e: targets monsters by default. */
    FF4_CmdT_DISABLED = 0x80   /**< d: command disabled. */
} FF4_CmdTargetFlags;

/* ========================================================================== */
/*  Magic types (menu $1B7E-$1B80; SpellListTbl ids $00-$0C)                  */
/* ========================================================================== */

typedef enum FF4_MagicType {
    FF4_MAGIC_WHITE = 0,       /**< White magic list. */
    FF4_MAGIC_BLACK = 1,       /**< Black magic list (incl. Edge's ninjutsu). */
    FF4_MAGIC_SUMMON = 2       /**< Summon ("Call") list. */
} FF4_MagicType;

/** Spell list storage ids for FF4_SPELL_LIST_COUNT lists ($1560-$1697). */
typedef enum FF4_SpellListId {
    FF4_SPELL_LIST_CECIL = 0,  /**< Cecil paladin white list (unused slot). */
    FF4_SPELL_LIST_UNUSED = 1, /**< Empty. */
    FF4_SPELL_LIST_RYDIA_W = 2,   /**< Rydia child white. */
    FF4_SPELL_LIST_RYDIA_B = 3,   /**< Rydia black (adult shares this). */
    FF4_SPELL_LIST_RYDIA_SUMMON = 4, /**< Rydia call. */
    FF4_SPELL_LIST_TELLAH_W = 5, /**< Tellah white. */
    FF4_SPELL_LIST_TELLAH_B = 6, /**< Tellah black. */
    FF4_SPELL_LIST_ROSA = 7,   /**< Rosa white. */
    FF4_SPELL_LIST_PALOM = 8,  /**< Palom black. */
    FF4_SPELL_LIST_POROM = 9,  /**< Porom white. */
    FF4_SPELL_LIST_FUSOYA_W = 10, /**< FuSoYa white (Golbez shares). */
    FF4_SPELL_LIST_FUSOYA_B = 11, /**< FuSoYa black (Golbez shares). */
    FF4_SPELL_LIST_EDGE = 12   /**< Edge ninjutsu. */
} FF4_SpellListId;

/* ========================================================================== */
/*  Spell ids (ROM spellNames $00-$47). US display names in comments.         */
/* ========================================================================== */

typedef enum FF4_SpellId {
    FF4_SPELL_NONE = 0x00, /**< Empty slot. */

    /* White magic ($01-$18). */
    FF4_SPELL_HOLD = 0x01,       /**< US "Hold" (paralyze). */
    FF4_SPELL_MUTE = 0x02,       /**< "Mute" (silence). */
    FF4_SPELL_CHARM = 0x03,      /**< "Charm". */
    FF4_SPELL_BLINK = 0x04,      /**< "Blink" (party image). */
    FF4_SPELL_ARMOR = 0x05,      /**< "Armor" (party protect). */
    FF4_SPELL_SHELL = 0x06,      /**< "Shell". */
    FF4_SPELL_SLOW = 0x07,       /**< "Slow". */
    FF4_SPELL_FAST = 0x08,       /**< "Fast" (haste). */
    FF4_SPELL_BERSERK = 0x09,    /**< "Berserk". */
    FF4_SPELL_WALL = 0x0A,       /**< "Wall" (reflect). */
    FF4_SPELL_WHITE = 0x0B,      /**< Engine "White" (dummy/utility). */
    FF4_SPELL_DUMMY_0C = 0x0C,   /**< Unused. */
    FF4_SPELL_PEEP = 0x0D,       /**< "Peep" (scan). */
    FF4_SPELL_CURE1 = 0x0E,      /**< "Cure1" (US "Cure"). */
    FF4_SPELL_CURE2 = 0x0F,      /**< "Cure2" (US "Cura"). */
    FF4_SPELL_CURE3 = 0x10,      /**< "Cure3" (US "Curaga"). */
    FF4_SPELL_CURE4 = 0x11,      /**< "Cure4" (US "Curaja"). */
    FF4_SPELL_HEAL = 0x12,       /**< "Heal" (cure poison/blind etc). */
    FF4_SPELL_LIFE1 = 0x13,      /**< "Life1" (US "Life"). */
    FF4_SPELL_LIFE2 = 0x14,      /**< "Life2" (US "Arise"). */
    FF4_SPELL_SIZE = 0x15,       /**< "Size" (mini). */
    FF4_SPELL_EXIT = 0x16,       /**< "Exit" (warp out of cave/tower). */
    FF4_SPELL_SIGHT = 0x17,      /**< "Sight" (reveal hidden passage). */
    FF4_SPELL_FLOAT = 0x18,      /**< "Float". */

    /* Black magic and utility ($19-$30). */
    FF4_SPELL_TOAD = 0x19,       /**< "Toad". */
    FF4_SPELL_PIGGY = 0x1A,      /**< "Piggy" (JP "Pig"). */
    FF4_SPELL_WARP = 0x1B,       /**< "Warp" (damage all enemies; US). */
    FF4_SPELL_VENOM = 0x1C,      /**< "Venom" (poison; US "Bio"?). */
    FF4_SPELL_FIRE1 = 0x1D,      /**< "Fire1" (US "Fire"). */
    FF4_SPELL_FIRE2 = 0x1E,      /**< "Fire2" (US "Fira"). */
    FF4_SPELL_FIRE3 = 0x1F,      /**< "Fire3" (US "Firaga"). */
    FF4_SPELL_ICE1 = 0x20,       /**< "Ice-1" (US "Blizzard"). */
    FF4_SPELL_ICE2 = 0x21,       /**< "Ice-2" (US "Blizzara"). */
    FF4_SPELL_ICE3 = 0x22,       /**< "Ice-3" (US "Blizzaga"). */
    FF4_SPELL_LIT1 = 0x23,       /**< "Lit-1" (US "Thunder"). */
    FF4_SPELL_LIT2 = 0x24,       /**< "Lit-2" (US "Thundara"). */
    FF4_SPELL_LIT3 = 0x25,       /**< "Lit-3" (US "Thundaga"). */
    FF4_SPELL_VIRUS = 0x26,      /**< "Virus" (darkness status). */
    FF4_SPELL_WEAK = 0x27,       /**< "Weak" (sap/doom-dose?). */
    FF4_SPELL_QUAKE = 0x28,      /**< "Quake". */
    FF4_SPELL_SLEEP = 0x29,      /**< "Sleep". */
    FF4_SPELL_STONE = 0x2A,      /**< "Stone" (petrify). */
    FF4_SPELL_FATAL = 0x2B,      /**< "Fatal" (death). */
    FF4_SPELL_STOP = 0x2C,       /**< "Stop". */
    FF4_SPELL_DRAIN = 0x2D,      /**< "Drain" (HP absorb). */
    FF4_SPELL_PSYCH = 0x2E,      /**< "Psych" (US "Banish"?). */
    FF4_SPELL_METEO = 0x2F,      /**< "Meteo" (US "Meteor"). */
    FF4_SPELL_NUKE = 0x30,       /**< "Nuke" (US "Flare"). */

    /* Summons ($31-$3F). */
    FF4_SPELL_IMP = 0x31,        /**< Imp (Rydia child). */
    FF4_SPELL_BOMB = 0x32,       /**< Bomb. */
    FF4_SPELL_DUMMY_33 = 0x33,   /**< Unused summon slot. */
    FF4_SPELL_MAGE = 0x34,       /**< Magus/Mage. */
    FF4_SPELL_CHOCBO = 0x35,     /**< Chocobo. */
    FF4_SPELL_SHIVA = 0x36,      /**< Shiva. */
    FF4_SPELL_INDRA = 0x37,      /**< Ramuh (JP "Indra"). */
    FF4_SPELL_JINN = 0x38,       /**< Jinn (Flame). */
    FF4_SPELL_TITAN = 0x39,      /**< Titan. */
    FF4_SPELL_MIST = 0x3A,       /**< Mist. */
    FF4_SPELL_SYLPH = 0x3B,      /**< Sylph. */
    FF4_SPELL_ODIN = 0x3C,       /**< Odin. */
    FF4_SPELL_LEVIA = 0x3D,      /**< Leviathan. */
    FF4_SPELL_ASURA = 0x3E,      /**< Asura. */
    FF4_SPELL_BAHAM = 0x3F,      /**< Bahamut. */

    /* Monster/spell-data only ($40-$47). */
    FF4_SPELL_COMET = 0x40,      /**< Comet. */
    FF4_SPELL_FLARE = 0x41,      /**< Flare. */
    FF4_SPELL_FLAME = 0x42,      /**< Flame (breath). */
    FF4_SPELL_FLOOD = 0x43,      /**< Flood. */
    FF4_SPELL_BLITZ = 0x44,      /**< Blitz. */
    FF4_SPELL_SMOKE = 0x45,      /**< Smoke (blind). */
    FF4_SPELL_PIN = 0x46,        /**< Pin (paralyze swarm). */
    FF4_SPELL_IMAGE = 0x47       /**< Image (blink). */
} FF4_SpellId;

/* ========================================================================== */
/*  Item ids (ROM itemNames $00-$FF). Names are the SNES (FF2 US) canon as   */
/*  decoded from the ROM ("Cure1" = Potion, "Ether1" = Ether, ...). Slots    */
/*  the US version blanked ("Dummy"/JP-only) are kept as named residents.    */
/* ========================================================================== */

typedef enum FF4_ItemId {
    FF4_ITEM_NONE = 0x00,         /**< Empty slot. */

    /* --- Claws --- */
    FF4_ITEM_FIRE_CLAW = 0x01,    /**< Fire Claw. */
    FF4_ITEM_ICE_CLAW = 0x02,     /**< Ice Claw. */
    FF4_ITEM_THUNDER_CLAW = 0x03, /**< Thunder Claw. */
    FF4_ITEM_CHARM_CLAW = 0x04,   /**< Charm Claw. */
    FF4_ITEM_VENOM_CLAW = 0x05,   /**< Venom Claw (JP: Poison). */
    FF4_ITEM_CAT_CLAW = 0x06,     /**< Cat Claw. */

    /* --- Rods --- */
    FF4_ITEM_ROD = 0x07,          /**< Rod. */
    FF4_ITEM_ICE_ROD = 0x08,      /**< Ice Rod. */
    FF4_ITEM_FLAME_ROD = 0x09,    /**< Flame Rod (Fire Rod). */
    FF4_ITEM_THUNDER_ROD = 0x0A,  /**< Thunder Rod. */
    FF4_ITEM_CHANGE_ROD = 0x0B,   /**< Change Rod. */
    FF4_ITEM_CHARM_ROD = 0x0C,    /**< Charm Rod. */
    FF4_ITEM_STARDUST_ROD = 0x0D, /**< Stardust Rod. */
    FF4_ITEM_LILITH_ROD = 0x0E,   /**< Lilith Rod. */

    /* --- Staves --- */
    FF4_ITEM_STAFF = 0x0F,        /**< Staff. */
    FF4_ITEM_CURE_STAFF = 0x10,   /**< Cure Staff. */
    FF4_ITEM_SILVER_STAFF = 0x11, /**< Silver Staff. */
    FF4_ITEM_POWER_STAFF = 0x12,  /**< Power Staff. */
    FF4_ITEM_LUNAR_STAFF = 0x13,  /**< Lunar Staff. */
    FF4_ITEM_LIFE_STAFF = 0x14,   /**< Life Staff. */
    FF4_ITEM_SILENCE_STAFF = 0x15,/**< Silence Staff. */

    /* --- Swords --- */
    FF4_ITEM_SHADOW_BLADE = 0x16, /**< Shadow Blade (Cecil's start sword). */
    FF4_ITEM_DARKNESS_SWORD = 0x17,/**< Darkness Sword. */
    FF4_ITEM_BLACK_SWORD = 0x18,  /**< Black Sword. */
    FF4_ITEM_LEGEND_SWORD = 0x19, /**< Legend Sword. */
    FF4_ITEM_LIGHT_SWORD = 0x1A,  /**< Light Sword (Ragnarok). */
    FF4_ITEM_EXCALBUR = 0x1B,     /**< Excalbur (JP-spelling). */
    FF4_ITEM_FLAME_SWORD = 0x1C,  /**< Flame Sword. */
    FF4_ITEM_ICE_BRAND = 0x1D,    /**< Ice Brand. */
    FF4_ITEM_DEFENSE_SWORD = 0x1E,/**< Defender ("Defense"). */
    FF4_ITEM_DRAIN_SWORD = 0x1F,  /**< Drain Sword ("Drain Blade"). */
    FF4_ITEM_ANCIENT_SWORD = 0x20,/**< Ancient Sword ("Rune Blade"). */
    FF4_ITEM_SLUMBER_SWORD = 0x21,/**< Slumber Sword ("Sleep Blade"). */
    FF4_ITEM_MEDUSA_SWORD = 0x22, /**< Medusa Blade ("Medusa"). */

    /* --- Spears --- */
    FF4_ITEM_SPEAR = 0x23,        /**< Spear. */
    FF4_ITEM_WIND_SPEAR = 0x24,   /**< Wind Spear. */
    FF4_ITEM_FLAME_SPEAR = 0x25,  /**< Flame Spear. */
    FF4_ITEM_BLIZZARD_SPEAR = 0x26,/**< Blizzard Spear. */
    FF4_ITEM_DRAGOON_SPEAR = 0x27,/**< Dragon Spear ("Dragoon"). */
    FF4_ITEM_WHITE_SPEAR = 0x28,  /**< White Spear ("Holy Lance"). */
    FF4_ITEM_DRAIN_SPEAR = 0x29,  /**< Drain Spear. */
    FF4_ITEM_GUNGNIR = 0x2A,      /**< Gungnir. */

    /* --- Katanas --- */
    FF4_ITEM_SHORT_KNIFE = 0x2B,  /**< Short Knife ("Short"). */
    FF4_ITEM_MIDDLE_KNIFE = 0x2C, /**< Middle Knife. */
    FF4_ITEM_LONG_KNIFE = 0x2D,   /**< Long Knife. */
    FF4_ITEM_NINJA_KNIFE = 0x2E,  /**< Ninja Knife. */
    FF4_ITEM_MURASAME = 0x2F,     /**< Murasame. */
    FF4_ITEM_MASAMUNE = 0x30,     /**< Masamune. */
    FF4_ITEM_ASSASSIN_KNIFE = 0x31,/**< Assassin Knife. */
    FF4_ITEM_MUTE_KNIFE = 0x32,   /**< Mute Knife. */

    /* --- Whips / hammers / axes --- */
    FF4_ITEM_WHIP = 0x33,         /**< Whip. */
    FF4_ITEM_CHAIN_WHIP = 0x34,   /**< Chain Whip. */
    FF4_ITEM_BLITZ_WHIP = 0x35,   /**< Blitz Whip. */
    FF4_ITEM_FLAME_WHIP = 0x36,   /**< Flame Whip. */
    FF4_ITEM_DRAGON_WHIP = 0x37,  /**< Dragon Whip. */
    FF4_ITEM_HAND_AXE = 0x38,     /**< Hand Axe. */
    FF4_ITEM_DWARF_AXE = 0x39,    /**< Dwarf Axe. */
    FF4_ITEM_OGRE_AXE = 0x3A,     /**< Ogre Axe (Ogrekiller). */
    FF4_ITEM_SILVER_AXE = 0x3B,   /**< Silver Axe ("Silver"). */
    FF4_ITEM_DANCING_DAGGER = 0x3C,/**< Dancing Dagger. */
    FF4_ITEM_SILVER_KNIFE = 0x3D, /**< Silver Knife. */
    FF4_ITEM_SPOON = 0x3E,        /**< Spoon. */
    FF4_ITEM_CRYSTAL_KNIFE = 0x3F,/**< Crystal Knife. */

    /* --- Throwing / misc weapons --- */
    FF4_ITEM_SHURIKEN = 0x40,     /**< Shuriken. */
    FF4_ITEM_NINJA_STAR = 0x41,   /**< Ninja Star. */
    FF4_ITEM_BOOMRANG = 0x42,     /**< Boomrang (Boomerang). */
    FF4_ITEM_FULL_MOON = 0x43,    /**< FullMoon. */
    FF4_ITEM_DREAMER = 0x44,      /**< Dreamer. */
    FF4_ITEM_CHARM_KNIFE = 0x45,  /**< Charm Knife. */
    FF4_ITEM_JPONLY_46 = 0x46,    /**< (JP) unused in US; JP-only weapon. */
    FF4_ITEM_POISON_KNIFE = 0x47, /**< Poison Knife ("Poison"). */
    FF4_ITEM_RUNE_AXE = 0x48,     /**< Rune Axe. */
    FF4_ITEM_SILVER_BOW_2 = 0x49, /**< Silver Bow ("Silver"). */
    FF4_ITEM_EARTH_BOW = 0x4A,    /**< Earth Bow ("Earth"). */
    FF4_ITEM_WOODEN_ARROW = 0x4B, /**< Wooden Arrow. */
    FF4_ITEM_AVENGER = 0x4C,      /**< Avenger. */

    /* --- Bows --- */
    FF4_ITEM_SHORT_BOW = 0x4D,    /**< Short Bow. */
    FF4_ITEM_CROSS_BOW = 0x4E,    /**< Cross Bow. */
    FF4_ITEM_GREAT_BOW = 0x4F,    /**< Great Bow. */
    FF4_ITEM_ARCHER_BOW = 0x50,   /**< Archer Bow ("Archer"). */
    FF4_ITEM_ELVEN_BOW = 0x51,    /**< Elven Bow. */
    FF4_ITEM_SAMURAI_BOW = 0x52,  /**< Samurai Bow. */
    FF4_ITEM_ARTEMIS_BOW = 0x53,  /**< Artemis Bow. */

    /* --- Arrows --- */
    FF4_ITEM_IRON_ARROW = 0x54,   /**< Iron Arrow. */
    FF4_ITEM_WHITE_ARROW = 0x55,  /**< White Arrow. */
    FF4_ITEM_FIRE_ARROW = 0x56,   /**< Fire Arrow. */
    FF4_ITEM_ICE_ARROW = 0x57,    /**< Ice Arrow. */
    FF4_ITEM_LIT_ARROW = 0x58,    /**< Lit Arrow (Lightning). */
    FF4_ITEM_DARKNESS_ARROW = 0x59,/**< Darkness Arrow. */
    FF4_ITEM_POISON_ARROW = 0x5A, /**< Poison Arrow. */
    FF4_ITEM_MUTE_ARROW = 0x5B,   /**< Mute Arrow. */
    FF4_ITEM_CHARM_ARROW = 0x5C,  /**< Charm Arrow. */
    FF4_ITEM_SAMURAI_ARROW = 0x5D,/**< Samurai Arrow. */
    FF4_ITEM_MEDUSA_ARROW = 0x5E, /**< Medusa Arrow. */
    FF4_ITEM_ARTEMIS_ARROW = 0x5F,/**< Artemis Arrow. */
    FF4_ITEM_DUMMY_60 = 0x60,     /**< (JP) unused. */

    /* --- Shields ($61-$76) --- */
    FF4_ITEM_IRON_SHIELD = 0x61,  /**< Iron Shield. */
    FF4_ITEM_SHADOW_SHIELD = 0x62,/**< Shadow Shield. */
    FF4_ITEM_BLACK_SHIELD = 0x63, /**< Black Shield. */
    FF4_ITEM_PALADIN_SHIELD = 0x64,/**< Paladin Shield. */
    FF4_ITEM_SILVER_SHIELD = 0x65,/**< Silver Shield. */
    FF4_ITEM_FLAME_SHIELD = 0x66, /**< Flame Shield. */
    FF4_ITEM_ICE_SHIELD = 0x67,   /**< Ice Shield. */
    FF4_ITEM_DIAMOND_SHIELD = 0x68,/**< Diamond Shield. */
    FF4_ITEM_AEGIS_SHIELD = 0x69, /**< Aegis Shield. */
    FF4_ITEM_SAMURAI_SHIELD = 0x6A,/**< Samurai Shield. */
    FF4_ITEM_DRAGOON_SHIELD = 0x6B,/**< Dragoon Shield. */
    FF4_ITEM_CRYSTAL_SHIELD = 0x6C,/**< Crystal Shield. */
    FF4_ITEM_IRON_SHIELD_2 = 0x6D,/**< (belt) Iron Shield (2nd tier copies). */
    FF4_ITEM_SHADOW_SHIELD_2 = 0x6E,/**< Shadow Shield (2nd tier). */
    FF4_ITEM_DARKNESS_SHIELD = 0x6F,/**< Darkness Shield. */
    FF4_ITEM_BLACK_SHIELD_2 = 0x70,/**< Black Shield (2nd tier). */
    FF4_ITEM_PALADIN_SHIELD_2 = 0x71,/**< Paladin Shield (2nd tier). */
    FF4_ITEM_SILVER_SHIELD_2 = 0x72,/**< Silver Shield (2nd tier). */
    FF4_ITEM_DIAMOND_SHIELD_2 = 0x73,/**< Diamond Shield (2nd tier). */
    FF4_ITEM_SAMURAI_SHIELD_2 = 0x74,/**< Samurai Shield (2nd tier). */
    FF4_ITEM_DRAGOON_SHIELD_2 = 0x75,/**< Dragoon Shield (2nd tier). */
    FF4_ITEM_CRYSTAL_SHIELD_2 = 0x76,/**< Crystal Shield (2nd tier). */

    /* --- Helmets ($77-$8C) --- */
    FF4_ITEM_CAP = 0x77,          /**< Cap. */
    FF4_ITEM_LEATHER_HAT = 0x78,  /**< Leather Hat. */
    FF4_ITEM_GAEA_HAT = 0x79,     /**< Gaea Hat ("Gaea"). */
    FF4_ITEM_WIZARD_HAT = 0x7A,   /**< Wizard Hat. */
    FF4_ITEM_TIARA = 0x7B,        /**< Tiara. */
    FF4_ITEM_RIBBON = 0x7C,       /**< Ribbon. */
    FF4_ITEM_HEADBAND = 0x7D,     /**< Headband. */
    FF4_ITEM_BANDANNA = 0x7E,     /**< Bandanna. */
    FF4_ITEM_NINJA_HOOD = 0x7F,   /**< Ninja Hood ("Ninja"). */
    FF4_ITEM_GLASS_HELMET = 0x80, /**< Glass Helmet. */
    FF4_ITEM_IRON_HELMET = 0x81,  /**< Iron Helmet. */
    FF4_ITEM_SHADOW_HELMET = 0x82,/**< Shadow Helmet. */
    FF4_ITEM_DARKNESS_HELMET = 0x83,/**< Darkness Helmet. */
    FF4_ITEM_BLACK_HELMET = 0x84, /**< Black Helmet. */
    FF4_ITEM_PALADIN_HELMET = 0x85,/**< Paladin Helmet. */
    FF4_ITEM_SILVER_HELMET = 0x86,/**< Silver Helmet. */
    FF4_ITEM_FLAME_HELMET = 0x87, /**< Flame Helmet. */
    FF4_ITEM_ICE_HELMET = 0x88,   /**< Ice Helmet. */
    FF4_ITEM_DIAMOND_HELMET = 0x89,/**< Diamond Helmet. */
    FF4_ITEM_SAMURAI_HELMET = 0x8A,/**< Samurai Helmet. */
    FF4_ITEM_DRAGOON_HELMET = 0x8B,/**< Dragoon Helmet. */
    FF4_ITEM_CRYSTAL_HELMET = 0x8C,/**< Crystal Helmet. */

    /* --- Body armour ($8D-$A6) --- */
    FF4_ITEM_CLOTHES = 0x8D,      /**< Clothes. */
    FF4_ITEM_LEATHER_ARMOR = 0x8E,/**< Leather Armor. */
    FF4_ITEM_GAEA_ARMOR = 0x8F,   /**< Gaea Armor. */
    FF4_ITEM_WIZARD_ROBE = 0x90,  /**< Wizard Robe. */
    FF4_ITEM_BLACK_ROBE = 0x91,   /**< Black Robe. */
    FF4_ITEM_SORCERER_ROBE = 0x92,/**< Sorcerer Robe. */
    FF4_ITEM_WHITE_ROBE = 0x93,   /**< White Robe. */
    FF4_ITEM_POWER_ROBE = 0x94,   /**< Power Robe. */
    FF4_ITEM_HEROINE_ROBE = 0x95, /**< Heroine Robe. */
    FF4_ITEM_PRISONER_ROBE = 0x96,/**< Prisoner Robe. */
    FF4_ITEM_BARD_ROBE = 0x97,    /**< Bard Robe. */
    FF4_ITEM_KARATE_GI = 0x98,    /**< Karate Gi. */
    FF4_ITEM_BLACK_BELT = 0x99,   /**< Black Belt ("Bl.Belt"). */
    FF4_ITEM_ADAMANT_ARMOR = 0x9A,/**< Adamant Armor (JP: Adamant). */
    FF4_ITEM_NINJA_ARMOR = 0x9B,  /**< Ninja Armor. */
    FF4_ITEM_IRON_ARMOR = 0x9C,   /**< Iron Armor. */
    FF4_ITEM_SHADOW_ARMOR = 0x9D, /**< Shadow Armor. */
    FF4_ITEM_DARKNESS_ARMOR = 0x9E,/**< Darkness Armor. */
    FF4_ITEM_BLACK_ARMOR = 0x9F,  /**< Black Armor. */
    FF4_ITEM_PALADIN_ARMOR = 0xA0,/**< Paladin Armor. */
    FF4_ITEM_SILVER_ARMOR = 0xA1, /**< Silver Armor. */
    FF4_ITEM_DIAMOND_ARMOR = 0xA2,/**< Diamond Armor. */
    FF4_ITEM_ZEUS_ARMOR = 0xA3,   /**< Zeus Armor ("Zeus"). */
    FF4_ITEM_SAMURAI_ARMOR = 0xA4,/**< Samurai Armor. */
    FF4_ITEM_DRAGOON_ARMOR = 0xA5,/**< Dragoon Armor. */
    FF4_ITEM_CRYSTAL_ARMOR = 0xA6,/**< Crystal Armor. */

    /* --- Rings / accessories ($A7-$AF) --- */
    FF4_ITEM_IRON_RING = 0xA7,    /**< Iron Ring. */
    FF4_ITEM_RUBY_RING = 0xA8,    /**< Ruby Ring. */
    FF4_ITEM_SILVER_RING = 0xA9,  /**< Silver Ring. */
    FF4_ITEM_STRENGTH_RING = 0xAA,/**< Strength Ring. */
    FF4_ITEM_RUNE_RING = 0xAB,    /**< Rune Ring. */
    FF4_ITEM_CRYSTAL_RING = 0xAC, /**< Crystal Ring. */
    FF4_ITEM_DIAMOND_RING = 0xAD, /**< Diamond Ring. */
    FF4_ITEM_PROTECT_RING = 0xAE, /**< Protect Ring. */
    FF4_ITEM_CURSED_RING = 0xAF,  /**< Cursed Ring. */

    /* --- (JP) armour forms blanked in the US build ($B0-$BF) --- */
    FF4_ITEM_JPONLY_B0 = 0xB0,
    FF4_ITEM_JPONLY_B1 = 0xB1,
    FF4_ITEM_JPONLY_B2 = 0xB2,
    FF4_ITEM_JPONLY_B3 = 0xB3,
    FF4_ITEM_JPONLY_B4 = 0xB4,
    FF4_ITEM_JPONLY_B5 = 0xB5,
    FF4_ITEM_JPONLY_B6 = 0xB6,
    FF4_ITEM_JPONLY_B7 = 0xB7,
    FF4_ITEM_JPONLY_B8 = 0xB8,
    FF4_ITEM_JPONLY_B9 = 0xB9,
    FF4_ITEM_JPONLY_BA = 0xBA,
    FF4_ITEM_JPONLY_BB = 0xBB,
    FF4_ITEM_JPONLY_BC = 0xBC,
    FF4_ITEM_JPONLY_BD = 0xBD,
    FF4_ITEM_JPONLY_BE = 0xBE,
    FF4_ITEM_JPONLY_BF = 0xBF,

    /* --- Consumables ($C0-$DD) --- */
    FF4_ITEM_FIRE_BOMB = 0xC0,    /**< Fire Bomb. */
    FF4_ITEM_JPONLY_C1 = 0xC1,    /**< (JP) unused. */
    FF4_ITEM_LIT_BOLT = 0xC2,     /**< Lit Bolt (Lightning bomb). */
    FF4_ITEM_JPONLY_C3 = 0xC3,
    FF4_ITEM_JPONLY_C4 = 0xC4,
    FF4_ITEM_JPONLY_C5 = 0xC5,
    FF4_ITEM_JPONLY_C6 = 0xC6,
    FF4_ITEM_JPONLY_C7 = 0xC7,
    FF4_ITEM_CRYSTAL = 0xC8,      /**< Crystal (J room key item). */
    FF4_ITEM_JPONLY_C9 = 0xC9,
    FF4_ITEM_JPONLY_CA = 0xCA,
    FF4_ITEM_JPONLY_CB = 0xCB,
    FF4_ITEM_JPONLY_CC = 0xCC,
    FF4_ITEM_JPONLY_CD = 0xCD,
    FF4_ITEM_CURE1 = 0xCE,        /**< Cure1 (US "Potion"). */
    FF4_ITEM_CURE2 = 0xCF,        /**< Cure2 (US "Hi-Potion"). */
    FF4_ITEM_CURE3 = 0xD0,        /**< Cure3 (US "X-Potion"). */
    FF4_ITEM_ETHER1 = 0xD1,       /**< Ether1 (US "Ether"). */
    FF4_ITEM_ETHER2 = 0xD2,       /**< Ether2 (US "Dry Ether"). */
    FF4_ITEM_ELIXIR = 0xD3,       /**< Elixir. */
    FF4_ITEM_LIFE = 0xD4,         /**< Life (US "Phoenix Down"). */
    FF4_ITEM_JPONLY_D5 = 0xD5,
    FF4_ITEM_JPONLY_D6 = 0xD6,
    FF4_ITEM_JPONLY_D7 = 0xD7,
    FF4_ITEM_JPONLY_D8 = 0xD8,
    FF4_ITEM_JPONLY_D9 = 0xD9,
    FF4_ITEM_JPONLY_DA = 0xDA,
    FF4_ITEM_JPONLY_DB = 0xDB,
    FF4_ITEM_JPONLY_DC = 0xDC,
    FF4_ITEM_HEAL = 0xDD,         /**< Heal (US "Maiden's Kiss"? id $DD). */
    FF4_ITEM_JPONLY_DE = 0xDE,
    FF4_ITEM_JPONLY_DF = 0xDF,

    /* --- Modes / rest items ($E0-$E6) --- */
    FF4_ITEM_JPONLY_E0 = 0xE0,
    FF4_ITEM_JPONLY_E1 = 0xE1,
    FF4_ITEM_TENT = 0xE2,         /**< Tent. */
    FF4_ITEM_CABIN = 0xE3,        /**< Cabin. */
    FF4_ITEM_JPONLY_E4 = 0xE4,
    FF4_ITEM_JPONLY_E5 = 0xE5,
    FF4_ITEM_JPONLY_E6 = 0xE6,

    /* --- Key items ($E7-$FA) --- */
    FF4_ITEM_IMP_KEY = 0xE7,      /**< Imp key item ("Imp"). */
    FF4_ITEM_BOMB_KEY = 0xE8,     /**< Bomb key item ("Bomb"). */
    FF4_ITEM_JPONLY_E9 = 0xE9,
    FF4_ITEM_MAGE_KEY = 0xEA,     /**< Mage key item ("Mage"). */
    FF4_ITEM_CARROT = 0xEB,       /**< Carrot (chocobo lure). */
    FF4_ITEM_PASS = 0xEC,         /**< Pass (Tower of Zot). */
    FF4_ITEM_WHISTLE = 0xED,      /**< Whistle (airship). */
    FF4_ITEM_PACKAGE = 0xEE,      /**< Package (Yang rescue). */
    FF4_ITEM_BARON_KEY = 0xEF,    /**< Baron key ("Baron"). */
    FF4_ITEM_SAND_RUBY = 0xF0,    /**< Sand Ruby. */
    FF4_ITEM_EARTH_CRYSTAL = 0xF1,/**< Earth Crystal ("Earth"). */
    FF4_ITEM_MAGMA_ROCK = 0xF2,   /**< Magma Rock ("Magma"). */
    FF4_ITEM_LUCA_KEY = 0xF3,     /**< Luca key ("Luca"). */
    FF4_ITEM_TWIN_HARP = 0xF4,    /**< Twin Harp. */
    FF4_ITEM_DARKNESS_KEY = 0xF5, /**< Darkness key ("Darkness"). */
    FF4_ITEM_RAT_KEY = 0xF6,      /**< Rat key ("Rat"). */
    FF4_ITEM_ADAMANT_ORE = 0xF7,  /**< Adamant ore (Legend Sword quest). */
    FF4_ITEM_PAN_KEY = 0xF8,      /**< Pan key ("Pan"). */
    FF4_ITEM_PINK_KEY = 0xF9,     /**< Pink key ("Pink"). */
    FF4_ITEM_TOWER_KEY = 0xFA,    /**< Tower key ("Tower"). */

    FF4_ITEM_JPONLY_FB = 0xFB,
    FF4_ITEM_JPONLY_FC = 0xFC,
    FF4_ITEM_JPONLY_FD = 0xFD,
    FF4_ITEM_SORT = 0xFE,         /**< Sort sentinel ("-Sort-"). */
    FF4_ITEM_TRASH_CAN = 0xFF     /**< Trash Can. */
} FF4_ItemId;

/* ========================================================================== */
/*  Equipment slot ids (char record $1030-$1036)                              */
/* ========================================================================== */

typedef enum FF4_EquipmentSlot {
    FF4_EQ_RIGHT_HAND = 0,  /**< Weapon (with arrow count). */
    FF4_EQ_LEFT_HAND = 1,   /**< Weapon or shield. */
    FF4_EQ_HEAD = 2,        /**< Helmet. */
    FF4_EQ_BODY = 3,        /**< Armour. */
    FF4_EQ_FINGER = 4       /**< Accessory/ring. */
} FF4_EquipmentSlot;

/* ========================================================================== */
/*  Stats ($100F-$1018 base, $1014-$1018 modified)                            */
/* ========================================================================== */

typedef enum FF4_StatId {
    FF4_STAT_STRENGTH = 0,  /**< $0F/$14. */
    FF4_STAT_AGILITY = 1,   /**< $10/$15. */
    FF4_STAT_STAMINA = 2,   /**< $11/$16 (vitality). */
    FF4_STAT_INTELLECT = 3, /**< $12/$17 (wisdom). */
    FF4_STAT_SPIRIT = 4     /**< $13/$18 (will). */
} FF4_StatId;

/* ========================================================================== */
/*  Worlds, map types, vehicles, directions, z-levels ($1700-$1707)           */
/* ========================================================================== */

typedef enum FF4_World {
    FF4_WORLD_OVERWORLD = 0,   /**< $1701: 0. */
    FF4_WORLD_UNDERGROUND = 1, /**< $1701: 1. */
    FF4_WORLD_MOON = 2         /**< $1701: 2. */
} FF4_World;

typedef enum FF4_MapType {
    FF4_MAP_OVERWORLD = 0,   /**< $1700: overworld. */
    FF4_MAP_UNDERGROUND = 1, /**< $1700: underground. */
    FF4_MAP_MOON = 2,        /**< $1700: moon. */
    FF4_MAP_SUB = 3          /**< $1700: sub-map (town/dungeon). */
} FF4_MapType;

typedef enum FF4_Vehicle {
    FF4_VEHICLE_NONE = 0,      /**< $1704: 0. */
    FF4_VEHICLE_CHOCOBO = 1,   /**< $1704: 1. */
    FF4_VEHICLE_BLACK_CHOCOBO = 2, /**< $1704: 2. */
    FF4_VEHICLE_HOVERCRAFT = 3,/**< $1704: 3. */
    FF4_VEHICLE_ENTERPRISE = 4,/**< $1704: 4. */
    FF4_VEHICLE_FALCON = 5,    /**< $1704: 5. */
    FF4_VEHICLE_BIG_WHALE = 6, /**< $1704: 6. */
    FF4_VEHICLE_SHIP = 7       /**< $1704: 7. */
} FF4_Vehicle;

typedef enum FF4_Direction {
    FF4_DIR_NONE = 0,  /**< Stationary. */
    FF4_DIR_UP = 1,    /**< $AB/$1705: 1. */
    FF4_DIR_RIGHT = 2, /**< 2. */
    FF4_DIR_DOWN = 3,  /**< 3. */
    FF4_DIR_LEFT = 4,  /**< 4. */
    FF4_DIR_SPIN = 8   /**< $1705 bit: spin while moving (chocobo). */
} FF4_Direction;

/* ========================================================================== */
/*  Battle runtime constants                                                 */
/*  ($1800-$1803 battle props; $352E action type; $D3/$D6 timers)            */
/* ========================================================================== */

/** Battle-over result flags ($1803 / 00/00A8 "grvu?f??"). */
typedef enum FF4_BattleEndFlags {
    FF4_BATTLE_FADEOUT = 0x01,  /**< f: fade out after battle. */
    FF4_BATTLE_UPDATE_PARTY = 0x02, /**< u: apply XP/level results. */
    FF4_BATTLE_VICTORY = 0x04,  /**< v: victory. */
    FF4_BATTLE_RAN = 0x08,      /**< r: escaped. */
    FF4_BATTLE_GAME_OVER = 0x10 /**< g: game over after defeat. */
} FF4_BattleEndFlags;

/** Engine action type ($352E). */
typedef enum FF4_ActionType {
    FF4_ACTION_CHOOSE_CHAR = 0, /**< Player character turn. */
    FF4_ACTION_CHOOSE_MONSTER = 1, /**< Monster AI turn. */
    FF4_ACTION_EXECUTE = 2,     /**< Execute queued attack. */
    FF4_ACTION_TIMER_EFFECT = 3 /**< Run a timer effect (poison/doom/...). */
} FF4_ActionType;

/** Timer kinds ($D6 timer duration function; timers array $2A04). */
typedef enum FF4_TimerType {
    FF4_TIMER_ATB = 0,        /**< Action/ATB gauge fill. */
    FF4_TIMER_IMMEDIATE_1 = 1,/**< Unused immediate. */
    FF4_TIMER_IMMEDIATE_2 = 2,/**< Immediate (instant action). */
    FF4_TIMER_MAGIC = 3,      /**< Spell cast delay. */
    FF4_TIMER_SLEEP = 4,      /**< Sleep recovery. */
    FF4_TIMER_PARALYZE = 5,   /**< Paralysis recovery. */
    FF4_TIMER_POISON = 6,     /**< Poison tick. */
    FF4_TIMER_PETRIFY = 7,    /**< Petrify progress. */
    FF4_TIMER_REFLECT = 8,    /**< Reflect/Wall duration. */
    FF4_TIMER_SAP = 9,        /**< Sap/HP-leak tick. */
    FF4_TIMER_STOP = 10,      /**< Stop duration. */
    FF4_TIMER_ITEM = 11,      /**< Item-use delay. */
    FF4_TIMER_COMMAND = 12    /**< Battle-command delay (Jump/...). */
} FF4_TimerType;

/** Speed-modifier cents ($103B, "speed multiplier", haste -3 / slow +8). */
typedef enum FF4_AtbSpeed {
    FF4_ATB_AUTO = 1,        /**< Auto-battle. */
    FF4_ATB_FASTEST = 12,    /**< 133%. */
    FF4_ATB_NORMAL = 16,     /**< 100%. */
    FF4_ATB_SLOWEST = 32     /**< 50%. */
} FF4_AtbSpeed;

/** Battle menu/mode settings ($16AC-$16BE). */
typedef enum FF4_BattleMode {
    FF4_BATTLE_MODE_WAIT = 0,  /**< Timers pause while in menus. */
    FF4_BATTLE_MODE_ACTIVE = 1,/**< Timers run during menus. */
    FF4_BATTLE_MODE_DELAY = 2  /**< ATB fills frozen until input. */
} FF4_BattleMode;

/* ========================================================================== */
/*  High-level target model for spells/items/commands                         */
/*  (matches attackProperties 'target' codes 0-7 and CmdTargetTbl bits)       */
/* ========================================================================== */

typedef enum FF4_TargetType {
    FF4_TARGET_NONE = 0,        /**< aptarget 0: no target / self. */
    FF4_TARGET_ONE_ALLY = 1,    /**< aptarget 1: single ally. */
    FF4_TARGET_ALL_ALLIES = 2,  /**< aptarget 2: whole party. */
    FF4_TARGET_ONE_ENEMY = 3,   /**< aptarget 3: single enemy. */
    FF4_TARGET_ALL_ENEMIES = 4, /**< aptarget 4: enemy group. */
    FF4_TARGET_DEAD_ALLY = 5,   /**< aptarget 5: revive target. */
    FF4_TARGET_DEAD_ENEMY = 6,  /**< aptarget 6: undead-targeted (JP). */
    FF4_TARGET_ALL_COMBAT = 7   /**< aptarget 7: everything. */
} FF4_TargetType;

/* ========================================================================== */
/*  Sound command codes (APU mailbox $1E00, used by the Phase 8 audio api)    */
/* ========================================================================== */

typedef enum FF4_SoundCommand {
    FF4_SND_NONE = 0x00,      /**< No pending sound command. */
    FF4_SND_PLAY_SONG = 0x01, /**< Play BGM track (param: track id). */
    FF4_SND_PLAY_SFX = 0x02,  /**< Play SFX (param: sfx id). */
    FF4_SND_FADE_IN = 0x04,   /**< Fade in (param: fade speed). */
    FF4_SND_SYSTEM_SFX = 0x10,/**< System cursor/confirm jingle. */
    FF4_SND_SET_PAN = 0x80,   /**< Set SFX panning. */
    FF4_SND_FADE_OUT_SLOW = 0x85,  /**< Slow fade out. */
    FF4_SND_FADE_OUT_FAST = 0x86,  /**< Fast fade out. */
    FF4_SND_FADE_HALF = 0x87,      /**< Fade to half volume. */
    FF4_SND_VOLUME_FULL = 0x88,    /**< Restore full volume. */
    FF4_SND_VOLUME_QUARTER = 0x8A, /**< Quarter volume. */
    FF4_SND_FADE_OUT = 0x8B,       /**< Fade out. */
    FF4_SND_SET_MONO = 0x90,       /**< Toggle mono. */
    FF4_SND_RESET = 0xFF           /**< Reset/reload SPC. */
} FF4_SoundCommand;

/* ========================================================================== */
/*  Misc system settings ($16A8-$16BE config area)                            */
/* ========================================================================== */

typedef enum FF4_RowSetting {
    FF4_ROWSET_3FRONT_2BACK = 0, /**< $16A8: 3 front / 2 back. */
    FF4_ROWSET_2FRONT_3BACK = 1  /**< $16A8: 2 front / 3 back. */
} FF4_RowSetting;

typedef enum FF4_TextSpeed {  /**< Battle message speed $16AD (0..5). */
    FF4_TEXT_FASTEST = 0,
    FF4_TEXT_SLOWEST = 5
} FF4_TextSpeed;

typedef enum FF4_ControllerMode {
    FF4_CTRL_SINGLE = 0,  /**< $16B8: one controller. */
    FF4_CTRL_MULTI = 1    /**< $16B8: multi-tap, per-character mapping. */
} FF4_ControllerMode;

/* ========================================================================== */
/*  Field flags (direct page $0600-$06FF area)                                */
/* ========================================================================== */

/** Tile property bits, sub-map tile prop byte (5-tile probe at $00A1). */
typedef enum FF4_TilePropFlags {
    FF4_TILE_DAMAGE = 0x01,      /**< d: damage tile. */
    FF4_TILE_HIDE_LZ = 0x02,     /**< z: hide sprite on lower z-level. */
    FF4_TILE_GRASS_WATER = 0x04, /**< g: grass/water (hide feet sprites). */
    FF4_TILE_EXIT = 0x08,        /**< e: exit tile (load previous map). */
    FF4_TILE_WATER_BG = 0x10,    /**< w: water battle bg / through-tile. */
    FF4_TILE_ENCOUNTERS = 0x20,  /**< b: enable random battles. */
    FF4_TILE_TRIGGER = 0x40      /**< t: trigger tile. */
} FF4_TilePropFlags;

/** Z-plane bits ($D2 "??????ud": u upstairs / d downstairs). */
typedef enum FF4_ZLevel {
    FF4_Z_GROUND = 0,
    FF4_Z_DOWNSTAIRS = 1, /**< d bit. */
    FF4_Z_UPSTAIRS = 2    /**< u bit. */
} FF4_ZLevel;

#ifdef __cplusplus
}
#endif

#endif /* FF4_TYPES_H */