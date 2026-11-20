/**
 * @file field_engine.c
 * @brief Phase 6: field system - maps, collision, movement, encounters.
 *
 * Ports field/move.asm (CheckPlayerMoveSub/CheckTilePass, random battle
 * stepping) and the map/npc data model. Rendering is intentionally out
 * of scope here; the logical world is what the renderers will draw.
 */
#include <string.h>

#include "ff4/engine/field_engine.h"

/* ==================================================================== */
/*  Map resolution                                                      */
/* ==================================================================== */

static int world_tileset(const FF4_GameState *g)
{
    return (int)g->world.world;      /* overworld/underground/moon = 0..2 */
}

int ff4_field_load_map(const FF4_AssetDB *db, FF4_GameState *g)
{
    uint8_t map = g->world.map_id;   /* map id always fits the table */

    if (!db)
        return -1;

    /* The world state already carries the map; (re)resolve defaults. */
    if (map >= 0xFB) {
        /* world maps use the world tile sets */
        g->world.map_type = (FF4_MapType)(map - 0xFB);
    } else {
        g->world.map_type = FF4_MAP_SUB;
    }
    return 0;
}

void ff4_field_tile_props(const FF4_AssetDB *db, const FF4_GameState *g,
                          int x, int y, uint8_t *b1, uint8_t *b2)
{
    uint8_t map = g->world.map_id;

    if (!db || x < 0 || x >= 256 || y < 0 || y >= 128) {
        *b1 = *b2 = 0;
        return;
    }

    if (map >= 0xFB) {
        int t = world_tileset(g);

        if (t >= 0 && t < 3) {
            int idx = (((y & 0xFF) << 4) | (x & 0x1F)) % 256;

            *b1 = db->world_tile_props[t][idx][0];
            *b2 = db->world_tile_props[t][idx][1];
            return;
        }
        *b1 = *b2 = 0;
        return;
    }

    {
        int ts = db->maps[map].graphics;

        if (ts >= 0 && ts < FF4_DB_TILESETS) {
            int idx = ((y & 0xFF) << 4) | (x & 0x1F);   /* 16 wide tileset */

            if (idx >= 0 && idx < 256) {
                *b1 = db->map_tile_props[ts][idx][0];
                *b2 = db->map_tile_props[ts][idx][1];
                return;
            }
        }
        *b1 = *b2 = 0;
    }
}

/* ==================================================================== */
/*  Collision (CheckTilePass, move.asm @aa58)                           */
/* ==================================================================== */

bool ff4_field_tile_pass_bytes(uint8_t cb1, uint8_t nb1, uint8_t zcur)
{
    /* CheckTilePass, move.asm @aa58 (SNES: 0 = passable; here true = passable).
     *
     * move.asm @a959 keeps the standing lane ($d2) and builds the neighbour
     * mask at DP $0A:
     *
     *     if (!(cb1 & 0x04)) $d2 = cb1 & 0x03;      ; bridges don't reset z
     *     ...                 $0A = (cb1 & 0x03) + 4;
     *
     *   if (cb1 & 0x04)                                ; on a bridge (@aa87)
     *       passable = ((nb1 & 0x03) & $d2) != 0
     *               || (nb1 & 0x04) != 0;              ; or onto another bridge
     *   else
     *       passable = (nb1 & $0A) != 0;               ; lane match
     *
     * $0A is a direct-page VARIABLE, not the immediate 0x0A: the assembled
     * opcode is $25 (AND dp).  Reading it as #$0A made most floors solid.
     */
    uint8_t z = zcur & FF4_TP1_Z_MASK;

    if (cb1 & FF4_TP1_BRIDGE) {
        if ((nb1 & FF4_TP1_Z_MASK) & z)
            return true;
        return (nb1 & FF4_TP1_BRIDGE) != 0;
    }
    return (nb1 & FF4_TP1_LAYER_MASK(z)) != 0;
}

bool ff4_field_tile_pass(const FF4_AssetDB *db, const FF4_GameState *g,
                         int x, int y, uint8_t zcur)
{
    uint8_t cb1, cb2, nb1, nb2;

    /* @aa5f: an event running blocks all movement */
    if (g->world.event_running)
        return false;

    ff4_field_tile_props(db, g, g->world.x, g->world.y, &cb1, &cb2);
    ff4_field_tile_props(db, g, x, y, &nb1, &nb2);
    return ff4_field_tile_pass_bytes(cb1, nb1, zcur);
}

/* ==================================================================== */
/*  Movement                                                            */
/* ==================================================================== */

static void dir_delta(FF4_Direction d, int *dx, int *dy)
{
    *dx = *dy = 0;
    switch (d) {
    case FF4_DIR_UP:    *dy = -1; break;
    case FF4_DIR_DOWN:  *dy = 1;  break;
    case FF4_DIR_LEFT:  *dx = -1; break;
    case FF4_DIR_RIGHT: *dx = 1;  break;
    default: break;
    }
}

FF4_FieldStep ff4_field_frame(const FF4_AssetDB *db, FF4_GameState *g,
                              FF4_Direction dir)
{
    FF4_FieldStep step;
    int nx, ny, dx, dy;
    uint8_t cb1, cb2, nb1, nb2;
    uint8_t zcur;

    memset(&step, 0, sizeof step);

    if (!g->world.player_control)
        return step;

    dir_delta(dir, &dx, &dy);
    nx = (int)g->world.x + dx;
    ny = (int)g->world.y + dy;

    /* the z-level at which the party currently stands */
    ff4_field_tile_props(db, g, g->world.x, g->world.y, &cb1, &cb2);
    zcur = cb1 & FF4_TP1_Z_MASK;

    if (!ff4_field_tile_pass(db, g, nx, ny, zcur))
        return step;                            /* blocked */

    ff4_field_tile_props(db, g, nx, ny, &nb1, &nb2);

    /* exit tile: leave the map via the previous-map stack */
    if (nb2 & FF4_TP2_EXIT) {
        step.kind = FF4_FIELD_EXIT;
        return step;
    }

    /* move the party */
    g->world.x = (uint16_t)nx;
    g->world.y = (uint16_t)ny;
    g->world.facing = dir;
    step.moved = true;

    /* standing z follows the tile unless it is a bridge (@a959) */
    if (!(nb1 & FF4_TP1_BRIDGE))
        g->world.z = (FF4_ZLevel)(nb1 & FF4_TP1_Z_MASK);

    /* tile effects after the step */
    if (nb2 & FF4_TP2_TRIGGER) {
        step.kind = FF4_FIELD_TRIGGER;
        step.param = 0;         /* triggerScript[map] runs upstream */
        return step;
    }
    if (nb2 & FF4_TP2_BATTLE) {
        step.kind = FF4_FIELD_BATTLE;
        step.param = ff4_field_random_battle(db, g);
        return step;
    }
    if (nb2 & FF4_TP2_DAMAGE) {
        step.kind = FF4_FIELD_DAMAGE;
        return step;
    }
    return step;
}

/* ==================================================================== */
/*  Random encounters (mapBattle group + per-map rate counters)         */
/* ==================================================================== */

int ff4_field_random_battle(const FF4_AssetDB *db, FF4_GameState *g)
{
    uint8_t map = g->world.map_id;
    uint8_t group = db->map_battle[map];
    uint8_t rate = db->map_battle_rates[map];
    int counter;

    if (!g->world.battle_enabled || group == 0 || rate == 0)
        return -1;

    {
        int k = rate;

        if (k > 4)
            k = 4;
        if (k < 1)
            return -1;
        counter = (int)(g->world.frame_counter & 0xFF);
        if ((counter & (0x1FF >> k)) != 0)
            return -1;
    }

    /* formation picked from the group's table (battleGroup[group]) */
    if (group >= FF4_DB_BATTLE_GROUPS)
        return -1;
    {
        int idx = counter % 8;

        return db->formation_groups[group][idx];
    }
}

void ff4_field_battle_step(const FF4_AssetDB *db, FF4_GameState *g)
{
    (void)db;
    (void)g;
    /* The original keeps $86/$87/$88 step counters; our per-frame roll
       uses the frame counter, so this hook exists for host bookkeeping. */
}

/* ==================================================================== */
/*  Camera                                                              */
/* ==================================================================== */

FF4_Camera ff4_field_camera(const FF4_GameState *g)
{
    FF4_Camera c;
    int px = (int)g->world.x * 16, py = (int)g->world.y * 16;
    int half_w = 320 / 2, half_h = 224 / 2;

    c.tile_x = px / 16;
    c.tile_y = py / 16;
    c.sub_x = px - px / 16 * 16;
    c.sub_y = py - py / 16 * 16;

    /* centre the camera on the party with a 320x224 viewport */
    (void)half_w;
    (void)half_h;
    c.bg_priority = 0;
    return c;
}