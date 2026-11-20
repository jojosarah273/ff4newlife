/**
 * @file asset_db.h
 * @brief Phase 2: typed game asset database.
 *
 * Loads the everything8215/ff4 `make rip` output (ff4-en-data.json) into
 * native C tables indexed by the same ids the engine uses (item ids, spell
 * ids, monster ids, formation ids, character ids...). Every gameplay table
 * in the JSON becomes a typed array; raw binary assets (graphics, palettes,
 * layouts, the RNG table, SPC data) are decoded from base64 into blobs.
 *
 * The rip companion file (vanilla/ff4-en-rip.json) supplies the byte->glyph
 * character table (text + DTE) used to decode dialogue in Phase 4.
 *
 * Loading is forgiving but loud: unparseable fields are skipped, a warning
 * is counted, and the rest of the table still loads. This mirrors the goal
 * of the port - the *logic* is authoritative; the data is just data.
 */
#ifndef FF4_ASSET_DB_H
#define FF4_ASSET_DB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ff4/core/ff4_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== table sizes (mirrors of ROM/JSON array lengths) ===== */
#define FF4_DB_ITEMS 256
#define FF4_DB_EQUIP 176
#define FF4_DB_SPELLS 72
#define FF4_DB_ATTACKS 256
#define FF4_DB_ATTACK_NAMES 184
#define FF4_DB_MONSTERS 224
#define FF4_DB_FORMATIONS 512
#define FF4_DB_CHARS 14
#define FF4_DB_LEVEL_CHARS 13
#define FF4_DB_LEVEL_MAX 68
#define FF4_DB_SHOPS 48
#define FF4_DB_MAPS 383
#define FF4_DB_MAP_TITLES 121
#define FF4_DB_BATTLE_GROUPS 80
#define FF4_DB_BATTLE_GROUPS_WORLD 16
#define FF4_DB_TILESETS 16
#define FF4_DB_TILES 256
#define FF4_DB_BLOBS 4096
#define FF4_DB_TEAMS 3
#define FF4_DB_SLOTS_IN_FORMATION 8

/* ===== attack definition (attackProperties row, 6 bytes decoded) ===== */
typedef struct FF4_AttackDef {
    uint8_t casting_time;    /**< Casting duration (frames-ish). */
    uint8_t target;          /**< FF4_TargetType raw code 0-7. */
    uint8_t power;           /**< Attack power. */
    uint8_t hit;             /**< Base hit%. */
    uint8_t effect;          /**< Effect id (status/cast table ref). */
    uint8_t element;         /**< Attack element bits (FF4_Element). */
    uint8_t mp_cost;         /**< MP cost if cast as magic. */
    bool boss;               /**< Boss-scaled parameters. */
    bool damage;             /**< Deals damage (else heal/buff). */
    bool split_damage;       /**< Splits across targets. */
    bool no_reflect;         /**< Ignores Wall/Reflect. */
} FF4_AttackDef;

/* ===== item (itemNames + itemProperties + itemEquipability + consumable) */
typedef struct FF4_ItemDef {
    uint8_t id;              /**< Slot id (= array index). */
    uint8_t symbol;          /**< itemSymbol: category used for equipability. */
    char name[10];           /**< Canonical SNES name ("Cure1", "FireClaw"). */

    /* price components (itemProperties: 256 * 3-byte rows). */
    uint8_t price10;         /**< Low price digit. */
    uint8_t price1000;       /**< High price digit (1000s). */
    uint8_t price_multiplier;/**< Shop price multiplier. */
    uint16_t equip_mask;     /**< itemEquipability[symbol]: which jobs equip. */

    bool is_consumable;      /**< Id in $B0-$DF consumable range. */
    FF4_AttackDef consumable;/**< itemConsumable[id-$B0] for potions etc. */
    uint8_t description_index; /**< itemDescriptionIndex (0 if none). */
} FF4_ItemDef;

/* ===== equipment (weaponArmorProperties row, 8 bytes decoded) ===== */
typedef struct FF4_EquipDef {
    uint8_t id;              /**< Item id this row belongs to ($00-$AF). */
    bool metallic;           /**< Affected by Magnetize. */
    bool shield;             /**< Is a shield. */
    uint8_t flags;           /**< Weapon flags (throw/two-hand/arrow bits). */
    uint8_t magic_defense_pct; /**< Mag. defence multiplier (armor). */
    uint8_t attack;          /**< Attack/defence power. */
    uint8_t attack_pct;      /**< Attack/defence percent. */
    uint8_t spell_cast;      /**< Weapon spell-cast (rod) reference. */
    uint8_t magic_defense;   /**< Mag. defence base. */
    uint8_t element_status;  /**< 7-bit ElementStatus table index. */
    uint8_t creature_type;   /**< Bonus vs. creature races (FF4_CreatureType). */
    uint8_t equipability;    /**< Class equipability flags. */
    uint8_t weapon_type;     /**< Bow/arrow/two-hand flags. */
    uint8_t stats;           /**< Stat-bonus selector. */
    uint8_t stat_mod;        /**< Stat-bonus amount. */

    /* Resolved via ElementStatus (attackElementStatus table). */
    uint8_t element;         /**< Attack element bits. */
    uint8_t status1;         /**< Status inflicted (bytes 1/2 layout). */
    uint8_t status2;         /**< Status inflicted (bytes 3/4 layout). */
} FF4_EquipDef;

/* ===== spell (spellNames + initial/learned lists) ===== */
typedef struct FF4_SpellDef {
    uint8_t id;
    uint8_t symbol;          /**< spellSymbol (visual/category). */
    char name[8];            /**< ROM name ("Fire1", "Ice-1", "Baham"). */
} FF4_SpellDef;

typedef struct FF4_SpellLearn {
    uint8_t spell;           /**< FF4_SpellId. */
    uint8_t level;           /**< Level at which it is learned. */
} FF4_SpellLearn;

/* ===== monster (monsterName + monsterProperties + attackDefense + items) */
typedef struct FF4_MonsterDef {
    uint8_t id;
    char name[9];

    uint8_t level;           /**< Monster level. */
    bool boss;               /**< Boss flag (Scarmiglione etc). */
    uint16_t hp;

    uint8_t attack;          /**< Attack base. */
    uint8_t magic_attack;    /**< Magic attack base. */
    uint8_t defense;
    uint8_t magic_defense;

    uint8_t agility;         /**< Index into the agility table. */
    uint8_t agility_min;     /**< monsterAgility[agility].min (ATB dur). */
    uint8_t agility_max;     /**< monsterAgility[agility].max. */

    uint8_t atk_def_base;    /**< monsterAttackDefense.base. */
    uint8_t atk_def_percent; /**< monsterAttackDefense.percent. */
    uint8_t atk_def_multi;   /**< monsterAttackDefense.multiplier. */

    uint8_t drop[4];         /**< Monster item set ids (FF4_ItemId). */
    uint8_t item_rate;       /**< Drop rate. */

    uint8_t script;          /**< Monster AI script index. */
    uint8_t counter;         /**< Retaliation/counter script index. */

    /* From the extended property fields. */
    uint8_t attack_elements; /**< Element bits of its attacks. */
    uint8_t strong_elements; /**< Element resistances (FF4_Element). */
    uint8_t weak_elements;   /**< Element weaknesses (FF4_Element). */
    uint8_t attack_status;   /**< Statuses its attacks inflict. */
    uint16_t status_immunity;/**< Immune statuses ($2B-$2C layout). */
    uint8_t creature_type;   /**< FF4_CreatureType. */
} FF4_MonsterDef;

/* ===== battle formation (battleProperties row, 8 bytes decoded) ===== */
typedef struct FF4_FormationDef {
    uint8_t mons[FF4_DB_TEAMS];       /**< Monster ids (0xFF = empty). */
    uint8_t counts[FF4_DB_TEAMS];     /**< Count per type. */
    bool eggs[FF4_DB_TEAMS];          /**< Egg monster types. */
    uint8_t position;                 /**< Monster position-data id. */
    uint8_t cursor_properties;        /**< Target-cursor data id. */
    uint8_t vram_map;                 /**< Background/graphics id. */
    uint8_t hidden;                   /**< Hidden monsters byte. */
    uint8_t song;                     /**< Battle music id (0-3). */
    uint8_t flags1;                   /**< Misc formation flags. */
    uint8_t flags2;                   /**< Misc formation flags. */
} FF4_FormationDef;

/* ===== character start data (CharProp + CharInitEquip) ===== */

typedef struct FF4_CharDef {
    uint8_t id;              /**< FF4_CharacterId this row belongs to. */
    uint8_t instance;        /**< Row index inside chars[]. */
    char name[7];            /**< e.g. "Cecil". */
    char class_name[8];      /**< e.g. "DKnight". */

    uint8_t init_equip;      /**< Index into the equipment table below. */
    uint8_t job;             /**< FF4_JobClass. */
    bool right_handed;
    bool left_handed;

    uint8_t level;
    uint16_t hp, mp;         /**< Max HP/MP. */
    uint16_t cur_hp, cur_mp; /**< Starting current values. */
    uint8_t str, agi, sta, intel, spr;
    uint8_t crit_pct, crit_bonus;
    uint8_t speed_mod;       /**< ATB speed multiplier. */
    uint32_t exp;            /**< Starting EXP. */
    uint32_t exp_last_level; /**< EXP at last level. */
    uint8_t start_status;    /**< Starting statuses byte 1. */

    /* Starting equipment (CharInitEquip row). */
    uint8_t eq_right, eq_right_qty;
    uint8_t eq_left, eq_left_qty;
    uint8_t eq_head, eq_body, eq_finger;
} FF4_CharDef;

/* ===== level up (LevelUpPropLow + LevelUpPropHigh, per character) ===== */
typedef struct FF4_LevelUpEntry {
    uint16_t exp_low;        /**< EXP for this level (low 16 bits). */
    uint8_t exp_msb;         /**< EXP high bits (3-byte value). */
    uint8_t hp;              /**< HP gained this level. */
    uint8_t mp;              /**< MP gained this level. */
    uint8_t stat_mod;        /**< Stat growth selector. */
    uint8_t stats;           /**< Stat growth amount. */
} FF4_LevelUpEntry;

/* ===== shop (shopProperties: 8 stocked items per shop) ===== */
typedef struct FF4_ShopDef {
    uint8_t items[8];        /**< FF4_ItemId per shelf (0xFF = empty). */
} FF4_ShopDef;

/* ===== map header (mapProperties row) ===== */
typedef struct FF4_MapDef {
    uint8_t id;
    char title[20];          /**< Resolved from mapTitle[id]. */
    bool hide_title;
    uint8_t song;            /**< BGM id. */
    uint8_t spell_flags;     /**< Spellcast allowed / flags byte. */
    uint8_t layout1;         /**< Tilemap assembly byte 1. */
    uint8_t layout2;         /**< Tilemap assembly byte 2. */
    uint8_t layout_msb;      /**< Tilemap MSBs. */
    uint8_t graphics;        /**< Tileset/gfx selection byte. */
    uint8_t fill;            /**< Fill tile. */
    uint8_t passable_fill;   /**< Passable-fill flag. */
    uint8_t palette;         /**< Palette assignment id. */
    uint8_t addition;        /**< Colour-addition flags. */
} FF4_MapDef;

/* ===== generic binary blob (base64 values from the JSON) ===== */
typedef struct FF4_Blob {
    char key[48];            /**< Object key ("rngTable", "windowGfx"...). */
    uint8_t *data;
    size_t len;
} FF4_Blob;

/* ===== byte->glyph conversion (from the rip json charTable) ===== */
typedef struct FF4_CharTable {
    bool text_valid[256];    /**< Plain 1-byte decode. */
    char text_glyph[256][8];
    bool dte_valid[256];     /**< DTE (2-byte compressed) decode. */
    char dte_glyph[256][8];
} FF4_CharTable;

/** One NPC record (npcProperties per-map lists; 14 slots per map). */
typedef struct FF4_NPCDef {
    uint8_t x, y;               /**< Starting tile position. */
    uint8_t speed;
    uint8_t direction;
    uint8_t reaction;           /**< Facing/behavior on talk. */
    uint8_t palette;
    bool npc_msb;               /**< $0FE5 n bit. */
    bool random_movement;
    bool animated;
    uint16_t switch0, switch1;  /**< Appearance switch gates. */
} FF4_NPCDef;

/* ===== the database ===== */
typedef struct FF4_AssetDB {
    bool loaded;             /**< True after a successful load. */
    char source[256];        /**< File that was loaded. */
    int warnings;            /**< Non-fatal parse issues encountered. */
    char error[160];         /**< Fatal load error message ("" if none). */

    /* typed tables */
    FF4_ItemDef items[FF4_DB_ITEMS];
    FF4_EquipDef equip[FF4_DB_EQUIP + 1];
    FF4_SpellDef spells[FF4_DB_SPELLS];
    FF4_AttackDef attacks[FF4_DB_ATTACKS];
    char attack_names[FF4_DB_ATTACK_NAMES][16];
    FF4_MonsterDef monsters[FF4_DB_MONSTERS];
    uint16_t monster_exp[FF4_DB_MONSTERS];  /**< Experience reward. */
    uint16_t monster_gp[FF4_DB_MONSTERS];   /**< Gil reward. */
    uint16_t item_classes[32];              /**< Equip masks by character id. */
    FF4_FormationDef formations[FF4_DB_FORMATIONS];
    uint8_t formation_groups[FF4_DB_BATTLE_GROUPS][8];
    uint8_t formation_groups_world[FF4_DB_BATTLE_GROUPS_WORLD][8];
    uint8_t monster_positions[256][FF4_DB_SLOTS_IN_FORMATION][2];
    FF4_CharDef chars[FF4_DB_CHARS];
    FF4_LevelUpEntry levelup[FF4_DB_LEVEL_CHARS][FF4_DB_LEVEL_MAX];
    uint8_t levelup_rows[FF4_DB_LEVEL_CHARS];
    FF4_ShopDef shops[FF4_DB_SHOPS];
    FF4_MapDef maps[FF4_DB_MAPS];
    char map_titles[FF4_DB_MAP_TITLES][20];
    uint8_t map_tile_props[FF4_DB_TILESETS][FF4_DB_TILES][2];
    uint8_t world_tile_props[3][FF4_DB_TILES][2]; /**< World tile props. */
    uint8_t world_triggers[3][58][7];
    uint8_t world_trigger_counts[3];
    char status_names[32][20];
    char battle_dialog[186][96];
    char battle_messages[59][64];
    char battle_command_names[32][12];  /**< Battle command display names. */
    uint8_t class_commands[22][5];      /**< Per-class battle command lists. */
    char map_dialog[384][128];      /**< Map dialogue (bank 0). */
    char event_dialog1[512][128];   /**< Event dialogue bank 1. */
    char event_dialog2[512][128];   /**< Event dialogue bank 2. */
    uint8_t map_battle[512];        /**< Random-battle group per map. */
    uint8_t map_battle_rates[512];  /**< Encounter rate per map. */

    /* NPCs per map (up to 14 per map, npcProperties). */
    FF4_NPCDef npcs[384][14];
    uint8_t npc_count[384];
    uint8_t spell_initial[FF4_DB_LEVEL_CHARS][FF4_SPELLS_PER_LIST];
    uint8_t spell_initial_count[FF4_DB_LEVEL_CHARS];
    FF4_SpellLearn spell_learned[FF4_DB_LEVEL_CHARS][24];
    uint8_t spell_learned_count[FF4_DB_LEVEL_CHARS];

    /* binary blobs */
    FF4_Blob blobs[FF4_DB_BLOBS];
    int blob_count;
    uint8_t rng_table[256];  /**< The game's RNG sequence (for Phase 3). */
    bool rng_table_valid;

    /* rip-level metadata */
    FF4_CharTable char_table;
} FF4_AssetDB;

/* =================================================================== */
/*  API                                                                 */
/* =================================================================== */

/**
 * Load the game database from ff4-en-data.json (everything8215/ff4 rip).
 * Returns 0 on success, -1 on fatal error (open/parse failure).
 */
int ff4_db_load(FF4_AssetDB *db, const char *path);

/**
 * Optionally load the companion rip file for the text character table.
 * Returns 0 on success, -1 if the file can't be read.
 */
int ff4_db_load_rip(FF4_AssetDB *db, const char *rip_json_path);

/** Free all allocated blobs. Safe to call on an unloaded db. */
void ff4_db_free(FF4_AssetDB *db);

/** Look up an arbitrary binary blob by object key. NULL if absent. */
const FF4_Blob *ff4_db_blob(const FF4_AssetDB *db, const char *key);

const char *ff4_item_name(const FF4_AssetDB *db, uint8_t id);
/** Monsters only resolve through the formation table (Phase 5 uses these). */
const FF4_MonsterDef *ff4_monster(const FF4_AssetDB *db, uint8_t id);
const FF4_CharDef *ff4_char(const FF4_AssetDB *db, uint8_t id);
const FF4_LevelUpEntry *ff4_level_entry(const FF4_AssetDB *db,
                                        uint8_t char_idx, uint8_t level);
uint32_t ff4_level_exp(const FF4_LevelUpEntry *e); /**< 3-byte EXP value. */
uint8_t ff4_glyph(const FF4_AssetDB *db, uint8_t byte); /**< '?' on miss. */

#ifdef __cplusplus
}
#endif

#endif /* FF4_ASSET_DB_H */