/**
 * @file field_engine.h
 * @brief Phase 6: field / overworld system.
 *
 * The field *scene* logic: map resolution, tile collision (CheckTilePass
 * port, move.asm @aa58), player movement with z-levels and bridge tiles,
 * tile triggers (events, exits, save points, damage tiles), NPCs and
 * random-battle stepping (mapBattle group + rate counters). Rendering
 * stays behind the platform layer; this module models the world.
 *
 * Data: sub-maps use mapProperties + mapTileProperties[tileset]; the
 * three worlds use worldTileProperties + world layout blobs and the
 * world trigger tables.
 */
#ifndef FF4_FIELD_ENGINE_H
#define FF4_FIELD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  Map resolution                                                      */
/* ==================================================================== */

/**
 * Resolve the world state's current map into a loaded field scene
 * (reads mapProperties, tileset, npc list, battle group/rate into
 * g.world and a scratch scene). Returns 0 on success.
 */
int ff4_field_load_map(const FF4_AssetDB *db, FF4_GameState *g);

/** Tile property bytes (b1/b2) for a logical map position. */
void ff4_field_tile_props(const FF4_AssetDB *db, const FF4_GameState *g,
                          int x, int y, uint8_t *b1, uint8_t *b2);

/* Tile property bits, verified against the ROM (see
   docs/RESEARCH-collision.md; the rip definition carries the names).

   byte1 = Passability:
     0x01 passable on the UPPER z-level
     0x02 passable on the LOWER z-level
     0x04 bridge tile
     0x08 save point
     0x10 door tile
   The CheckTilePass floor test is (byte1 & 0x0A) != 0 (LowerZ|SavePoint).

   byte2 = Flags:
     0x01 damage tile        0x02 unknown
     0x04 hide sprite (lower z)  0x08 hide bottom of sprite
     0x10 exit to previous map   0x20 through-tile
     0x40 enable random battles  0x80 trigger tile */
/* byte 1 bits 0-1 are LANE-PASSABLE FLAGS, bit 2 is "bridge".
 *
 * CheckTilePass (move.asm @aa58) ANDs the neighbour with a mask held in the
 * direct-page variable $0A, which @a965 builds as:
 *
 *     lda $a1 : and #$03 : clc : adc #$04 : sta $0a      ; mask = z + 4
 *
 * The listing proves the operand is `25 0A` = AND $0A (direct page), NOT
 * `and #$0a`.  So the rule is a lane match (plus bridges), not a fixed 0x0A:
 *
 *     passable = (next_byte1 & ((z & 3) + 4)) != 0
 */
#define FF4_TP1_LAYER_MASK(z) (((z) & FF4_TP1_Z_MASK) + FF4_TP1_BRIDGE)

typedef enum FF4_TilePropB1 {
    FF4_TP1_UPPER_Z = 0x01,    /**< Passable on upper z-level. */
    FF4_TP1_LOWER_Z = 0x02,    /**< Passable on lower z-level. */
    FF4_TP1_Z_MASK = 0x03,     /**< Tile's z-level bits. */
    FF4_TP1_BRIDGE = 0x04,     /**< Bridge tile. */
    FF4_TP1_SAVE = 0x08,       /**< Save point. */
    FF4_TP1_DOOR = 0x10        /**< Door tile. */
} FF4_TilePropB1;

typedef enum FF4_TilePropB2 {
    FF4_TP2_DAMAGE = 0x01,     /**< Damage tile. */
    FF4_TP2_UNKNOWN = 0x02,    /**< Unknown 1.1. */
    FF4_TP2_HIDE_Z = 0x04,     /**< Hide sprite on lower z-level. */
    FF4_TP2_HIDE_BOTTOM = 0x08,/**< Hide bottom of sprite. */
    FF4_TP2_EXIT = 0x10,       /**< Exit: load the previous map. */
    FF4_TP2_THROUGH = 0x20,    /**< Through-tile. */
    FF4_TP2_BATTLE = 0x40,     /**< Enable random battles here. */
    FF4_TP2_TRIGGER = 0x80     /**< Trigger tile. */
} FF4_TilePropB2;

/**
 * CheckTilePass (move.asm @aa58): can the player enter (x, y) given the
 * z-level at which they currently stand? Ports the bridge/simple branches
 * exactly: blocked while events run, by NPCs, and when the neighbour
 * carries the "blocks layer n" bit for the standing layer.
 */
bool ff4_field_tile_pass_bytes(uint8_t cb1, uint8_t nb1, uint8_t zcur);
bool ff4_field_tile_pass(const FF4_AssetDB *db, const FF4_GameState *g,
                         int x, int y, uint8_t zcur);

/* ==================================================================== */
/*  Movement                                                            */
/* ==================================================================== */

typedef enum FF4_FieldEventKind {
    FF4_FIELD_NONE = 0,
    FF4_FIELD_TRIGGER,     /**< Trigger event fired (script). */
    FF4_FIELD_EXIT,        /**< Stepped on an exit tile. */
    FF4_FIELD_SAVE,        /**< Save point. */
    FF4_FIELD_DAMAGE,      /**< Damage tile. */
    FF4_FIELD_BATTLE       /**< Random encounter (formation id). */
} FF4_FieldEventKind;

typedef struct FF4_FieldStep {
    FF4_FieldEventKind kind;
    int param;             /**< Formation id / trigger script id. */
    bool moved;            /**< The party actually moved. */
} FF4_FieldStep;

/**
 * One field frame: try to move one tile in `dir` (player control only).
 * Checks passability, moves, updates z, and reports tile effects
 * (trigger/exit/save/damage; random battles handled via
 * ff4_field_random_battle when a battle tile is entered).
 */
FF4_FieldStep ff4_field_frame(const FF4_AssetDB *db, FF4_GameState *g,
                              FF4_Direction dir);

/**
 * Random battle roll after stepping on a battle-enabled tile
 * (uses mapBattle[map] group + the game's step/rate counters).
 * Returns a formation id (and flags a battle) or -1.
 */
int ff4_field_random_battle(const FF4_AssetDB *db, FF4_GameState *g);

/** Mark an encounter as rolled for the current counter state. */
void ff4_field_battle_step(const FF4_AssetDB *db, FF4_GameState *g);

/* ==================================================================== */
/*  Camera (abstract; consumed by renderers)                            */
/* ==================================================================== */

typedef struct FF4_Camera {
    int tile_x, tile_y;    /**< Top-left tile visible. */
    int sub_x, sub_y;      /**< Pixel offset within the tile (0-15). */
    int bg_priority;       /**< Which layer draws first. */
} FF4_Camera;

/** Compute the camera framing for the player position. */
FF4_Camera ff4_field_camera(const FF4_GameState *g);

#ifdef __cplusplus
}
#endif

#endif /* FF4_FIELD_ENGINE_H */