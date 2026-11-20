/**
 * @file scene_machine.c
 * @brief Phase 9: scene transitions (field / battle / menu / shop) +
 *        the event-16 scripted opening (Phase 11).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ff4/engine/scene_machine.h"
#include "ff4/engine/battle_engine.h"
#include "ff4/engine/event_engine.h"
#include "ff4/engine/field_engine.h"

void ff4_scene_init(FF4_SceneCtx *sc)
{
    memset(sc, 0, sizeof *sc);
    sc->scene = FF4_SCENE_OPENING;
    sc->opening = FF4_OPENING_TITLE;
    sc->pending_formation = -1;
    sc->shop_id = -1;
    sc->intro.map = -1;
    sc->intro.host_song = -1;
}

void ff4_scene_log(FF4_SceneCtx *sc, const char *fmt, ...)
{
    va_list ap;
    char buf[64];

    if (sc->log_count >= 8)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    snprintf(sc->log[sc->log_count++], sizeof sc->log[0], "%s", buf);
}

void ff4_scene_request_battle(FF4_SceneCtx *sc, int formation)
{
    sc->pending_formation = formation;
}

void ff4_scene_request_menu(FF4_SceneCtx *sc)
{
    sc->menu_open = true;
}

void ff4_scene_advance_opening(FF4_SceneCtx *sc)
{
    /* title -> intro -> field; the intro itself is driven by the
       game's own event script inside ff4_scene_intro_frame() */
    if (++sc->opening >= FF4_OPENING_END) {
        sc->opening = FF4_OPENING_TITLE;
        sc->scene = FF4_SCENE_FIELD;
    }
}

void ff4_scene_close(FF4_SceneCtx *sc)
{
    sc->scene = FF4_SCENE_FIELD;
    sc->menu_open = false;
    sc->pending_formation = -1;
}

void ff4_scene_request_shop(FF4_SceneCtx *sc, int shop)
{
    sc->shop_id = shop;
    sc->scene = FF4_SCENE_SHOP;
    sc->menu_open = true;
}

/* ==================================================================== */
/*  Scripted opening (event 16 = the game's intro event)                */
/* ==================================================================== */

void ff4_scene_intro_ack(FF4_SceneCtx *sc)
{
    sc->intro.dialog_open = false;
    ff4_event_ack(&sc->intro.run);
}

/* event object ids 0..9 on the intro maps: the player is obj 0,
   the others map to the current map's NPC slots 0..14 (Cecil, Kain,
   the captain, the crew; the Mysidia elder...). */
static void intro_actor_from_npc(const FF4_AssetDB *db, FF4_SceneCtx *sc,
                                 int obj)
{
    FF4_Intro *in = &sc->intro;

    if (obj <= 0 || obj > 15)
        return;
    if (in->map < 0 || (unsigned)in->map >= FF4_DB_MAPS)
        return;
    {
        int slot = obj - 1;

        if (slot < db->npc_count[in->map]) {
            const FF4_NPCDef *np = &db->npcs[in->map][slot];
            int gid = 0, m;

            /* global npc index = sum of counts over previous maps */
            for (m = 0; m < in->map; ++m)
                gid += db->npc_count[m];
            gid += slot;
            in->actor[obj].x = np->x;
            in->actor[obj].y = np->y;
            in->actor[obj].dir =
                np->direction > 3 ? 2 : (int)np->direction;
            in->actor[obj].palette = np->palette;
            {
                char key[48];

                /* the flat npc id table wraps; bound it so the intro
                   crew still gets a plausible sprite */
                snprintf(key, sizeof key, "npcGraphicsProperties[%d]",
                         gid & 0x1FF);
                {
                    const FF4_Blob *sb = ff4_db_blob(db, key);

                    if (sb && sb->len)
                        in->actor[obj].sprite = sb->data[0];
                }
            }
            in->actor[obj].visible = true;
        }
    }
}

static void intro_warp(FF4_SceneCtx *sc, FF4_GameState *g)
{
    FF4_Intro *in = &sc->intro;
    uint8_t map = g->world.map_id;

    in->actor_count = 0;
    in->map = map >= 0xFB ? -1 : (int)map;
    if (map >= 0xFB)
        in->world = map - 0xFB;
    in->cam_x = (int)g->world.x - 15;
    if (in->cam_x < 0)
        in->cam_x = 0;
    in->cam_y = (int)g->world.y >= 14 ? (int)g->world.y - 14 : 0;
    in->cam_px = 0;
    in->cam_py = 0;
}

void ff4_scene_intro_begin(const FF4_AssetDB *db, FF4_GameState *g,
                           FF4_SceneCtx *sc)
{
    FF4_Intro *in = &sc->intro;
    const FF4_Blob *b = ff4_db_blob(db, "eventScript[16]");

    memset(in, 0, sizeof *in);
    in->map = -1;
    in->world = 0;
    in->fade = 15;
    in->host_song = -1;
    in->cam_x = 0;
    in->cam_y = 96;
    in->fly_frame = 0;
    in->done = false;

    if (b)
        ff4_event_run_begin(&in->run, g, db, b->data, b->len);
    sc->opening = FF4_OPENING_INTRO;
}

bool ff4_scene_intro_frame(const FF4_AssetDB *db, FF4_GameState *g,
                           FF4_SceneCtx *sc)
{
    FF4_Intro *in = &sc->intro;

    if (in->done)
        return false;

    /* blocked on a dialogue: the host acked it with Z */
    if (in->dialog_open)
        return true;

    /* in auto mode host dialogs auto-advance (the driver re-enters
       here after ff4_scene_intro_ack cleared the previous one) */
    if (in->run.state == FF4_EVENT_BLOCKED && in->run.host_blocked)
        ff4_event_ack(&in->run);

    ff4_event_tick(&in->run, 1);

    if (in->run.state == FF4_EVENT_DONE) {
        ff4_scene_log(sc, "Intro finished -> field");
        /* the last warp in event 16 = map $24 (Baron) */
        g->world.map_id = (uint8_t)(in->map >= 0 ? in->map : 0x24);
        g->world.map_type = FF4_MAP_SUB;
        g->world.x = 16;
        g->world.y = 30;
        ff4_field_load_map(db, g);
        in->done = true;
        sc->scene = FF4_SCENE_FIELD;
        return false;
    }

    /* execute non-blocked commands until the next host action */
    {
    int steps = 0;
    for (steps = 0; steps < 200; ++steps) {
        FF4_EventAction a = ff4_event_step(&in->run);

        if (in->run.state == FF4_EVENT_DONE || a.kind == FF4_EVT_ACT_NONE)
            break;
        switch (a.kind) {
        case FF4_EVT_ACT_DIALOGUE:
            snprintf(in->dialog_text, sizeof in->dialog_text, "%s",
                     a.text ? a.text : "");
            in->dialog_open = true;
            return true;              /* host advances with Z */
        case FF4_EVT_ACT_YESNO:
        case FF4_EVT_ACT_ITEM_SELECT:
            ff4_event_ack(&in->run);  /* auto-answer */
            break;
        case FF4_EVT_ACT_MOVE:
            /* act nibble 0..3 = face, 4..7 = step: animate */
            if (a.obj < 16) {
                if (a.act >= 0 && a.act < 4)
                    in->actor[a.obj].dir = a.act;
                else
                    in->actor[a.obj].step++;
                intro_actor_from_npc(db, sc, a.obj);
            }
            break;
        case FF4_EVT_ACT_GFX_CHANGE:
            break;
        case FF4_EVT_ACT_CINEMATIC:
            if (a.param == 0)
                in->map = -1;         /* flyover: world view */
            ff4_event_ack(&in->run);  /* cutscene handled -> unblock */
            break;
        case FF4_EVT_ACT_SCREEN:
            if (a.param == (int)FF4_EVT_MAP)
                intro_warp(sc, g);    /* event 0xFE warp */
            else if (a.param == (int)FF4_EVT_SCREEN_FADE)
                in->fade = a.param2 == 0 ? 0 : 15;
            else if (a.param == (int)FF4_EVT_SCREEN_FLASH)
                in->flash = 240;
            break;
        case FF4_EVT_ACT_BATTLE:
            ff4_scene_log(sc, "Opening battle %d cleared", a.param);
            ff4_event_ack(&in->run);  /* skip; keep the flow going */
            break;
        case FF4_EVT_ACT_SONG:
            in->host_song = a.param;
            break;
        case FF4_EVT_ACT_WAIT:
        default:
            break;
        }
        if (in->run.state == FF4_EVENT_BLOCKED)
            break;
    }
    }
    (void)db;
    if (getenv("FF4_TRACE"))
        fprintf(stderr, "[post] frames rc, map=%d pc=%zu st=%d\n",
                in->map, in->run.pc, (int)in->run.state);
    if (in->map < 0)
        ++in->fly_frame;
    return true;
}

bool ff4_scene_frame(const FF4_AssetDB *db, FF4_GameState *g,
                     FF4_SceneCtx *sc, FF4_Direction field_dir)
{
    /* the opening screens are host-drawn stills; the machine only
       tracks the stage until the player moves past them */
    if (sc->scene == FF4_SCENE_OPENING) {
        if (sc->opening >= FF4_OPENING_END) {
            sc->scene = FF4_SCENE_FIELD;
            return true;
        }
        if (sc->opening == FF4_OPENING_INTRO && !sc->intro.done)
            ff4_scene_intro_frame(db, g, sc);
        return true;
    }

    if (sc->scene == FF4_SCENE_FIELD) {
        FF4_FieldStep step = ff4_field_frame(db, g, field_dir);
        static uint16_t battle_step;

        if (step.kind == FF4_FIELD_BATTLE && step.param >= 0) {
            sc->pending_formation = step.param;
            sc->scene = FF4_SCENE_BATTLE;
            ff4_scene_log(sc, "Encounter! group %d", step.param);
            return true;
        }
        if (step.kind == FF4_FIELD_EXIT)
            return true;
        if (sc->pending_formation >= 0) {
            FF4_BattleOpts o;

            memset(&o, 0, sizeof o);
            o.battle_bg = sc->pending_formation & 0x0F;
            if (ff4_battle_start(db, g, (uint8_t)sc->pending_formation,
                                 &o) == 0) {
                sc->scene = FF4_SCENE_BATTLE;
                ff4_scene_log(sc, "Battle %d starts", sc->pending_formation);
            }
            sc->pending_formation = -1;
            return true;
        }
        if (sc->menu_open) {
            sc->scene = FF4_SCENE_MENU;
            sc->menu_open = false;
            return true;
        }
        (void)battle_step;
        return true;
    }

    if (sc->scene == FF4_SCENE_BATTLE) {
        FF4_Rng rng;
        bool defeat = false;
        int ready;

        memcpy(rng.table, g->battle.rng_table, 256);
        rng.ptr = g->battle.rng_ptr;

        ff4_battle_atb(db, g, 2);
        ff4_battle_timer_effects(g, 1);

        if (ff4_battle_check_end(&g->battle, &defeat)) {
            uint32_t gained_exp = 0, gained_gil = 0;

            /* capture rewards before the finish resets the battle state */
            if (!defeat) {
                ff4_battle_win_rewards(&rng, db, g);
                gained_exp = g->battle.exp_gained;
                gained_gil = g->battle.gil_gained;
                ff4_scene_log(sc, "Victory! +%u exp, +%u gil",
                              gained_exp, gained_gil);
            } else {
                ff4_scene_log(sc, "Defeated...");
            }
            ff4_battle_sync_back(g);
            ff4_battle_finish(&g->battle);
            sc->scene = FF4_SCENE_FIELD;
            return true;
        }

        ready = ff4_battle_next_ready(&g->battle, g->battle.last_actor);
        if (ready >= 0) {
            ff4_battle_auto_command(&g->battle, ready);
            ff4_battle_execute(&rng, db, g, ready);
            ff4_battle_update_dead(&g->battle);
        }
        g->battle.rng_ptr = rng.ptr;
        return true;
    }

    /* MENU / SHOP overlays: the host drives FF4_MenuCtx directly with
       input calls; the machine only tracks the returned scene. */
    if (sc->scene == FF4_SCENE_MENU || sc->scene == FF4_SCENE_SHOP) {
        if (sc->menu_open) {
            sc->scene = FF4_SCENE_FIELD;
            sc->menu_open = false;
        }
        return true;
    }
    return false;
}