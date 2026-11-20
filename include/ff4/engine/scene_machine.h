/**
 * @file scene_machine.h
 * @brief Phase 9: scene machine.
 *
 * The host-side scene stack: FIELD <-> BATTLE and FIELD <-> MENU
 * transitions, mirroring the original's scene juggling (field events
 * launch battles, menus overlay; victory returns to the field with the
 * world state intact). The machine owns the current scene and the
 * transitions' one-frame handshake (fade-outs etc. are left to the
 * renderer).
 */
#ifndef FF4_SCENE_MACHINE_H
#define FF4_SCENE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"
#include "ff4/engine/event_engine.h"
#include "ff4/engine/menu_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FF4_Scene {
    FF4_SCENE_OPENING = 0, /**< Title + intro event-driven opening. */
    FF4_SCENE_FIELD,       /**< Exploration (field engine). */
    FF4_SCENE_BATTLE,      /**< Combat (battle engine). */
    FF4_SCENE_MENU,        /**< Main menu overlay. */
    FF4_SCENE_SHOP         /**< Shop overlay. */
} FF4_Scene;

/** Opening stages: title, then the scripted intro (event 16). */
typedef enum FF4_OpeningStage {
    FF4_OPENING_TITLE = 0,
    FF4_OPENING_INTRO,     /**< Event-16 opening (flyover, ship, Mysidia). */
    FF4_OPENING_END
} FF4_OpeningStage;

/** One actor on the screen (event object id -> sprite + position). */
typedef struct FF4_IntroActor {
    int x, y;              /**< Tile position (converted to pixels by host). */
    int dir;               /**< 0 up, 1 right, 2 down, 3 left. */
    int step;              /**< Walk animation phase. */
    int sprite;            /**< mapSpriteGraphics id (-1 = none). */
    int palette;           /**< npcPalettes id. */
    bool visible;
} FF4_IntroActor;

/**
 * The scripted opening: runs real event 16 through FF4_EventRun so the
 * airship flyover, the ship scene, the Mysidia flashback and the two
 * guard battles happen in the game's own order with its own text.
 */
typedef struct FF4_Intro {
    FF4_EventRun run;      /**< Event 16 interpreter. */

    int map;               /**< Current sub-map (-1 = world flyover). */
    int world;             /**< World map type during the flyover. */
    int cam_x, cam_y;      /**< Camera tiles. */
    int cam_px, cam_py;    /**< Camera pixel sub-offset. */
    int fly_frame;         /**< Flyover animation frame. */
    bool done;             /**< Intro finished -> field scene. */

    bool dialog_open;      /**< Host shows the dialogue window now. */
    char dialog_text[256]; /**< Current dialogue line. */
    int host_song;         /**< Last requested song (-1 = none). */
    int fade;              /**< 0..15 screen brightness (15 = full). */
    int flash;

    FF4_IntroActor actor[16]; /**< Event objects 0..15. */
    int actor_count;
} FF4_Intro;

typedef struct FF4_SceneCtx {
    FF4_Scene scene;
    FF4_OpeningStage opening;  /**< Current opening screen stage. */
    FF4_Intro intro;           /**< Event-driven opening (OPENING_INTRO). */

    /* pending transitions */
    int pending_formation; /**< -1 = none; else start that battle. */
    bool menu_open;        /**< Main menu requested. */
    int shop_id;           /**< Shop requested (-1 = none). */

    /* message log (battle summaries, item pickups...) */
    char log[8][64];
    int log_count;
} FF4_SceneCtx;

void ff4_scene_init(FF4_SceneCtx *sc);
void ff4_scene_log(FF4_SceneCtx *sc, const char *fmt, ...);

/* transitions */
void ff4_scene_request_battle(FF4_SceneCtx *sc, int formation);
void ff4_scene_request_menu(FF4_SceneCtx *sc);
void ff4_scene_request_shop(FF4_SceneCtx *sc, int shop);
void ff4_scene_close(FF4_SceneCtx *sc);   /**< Return to the field scene. */

/** Advance the opening screens (title -> intro -> field). */
void ff4_scene_advance_opening(FF4_SceneCtx *sc);

/** Begin the event-16 intro: flyover then the scripted scenes. */
void ff4_scene_intro_begin(const FF4_AssetDB *db, FF4_GameState *g,
                           FF4_SceneCtx *sc);

/** Advance the intro by one frame; blocked dialogs stay open until
 *  the host calls ff4_scene_intro_ack() (Z). Returns false when the
 *  intro finished and the field scene took over. */
bool ff4_scene_intro_frame(const FF4_AssetDB *db, FF4_GameState *g,
                           FF4_SceneCtx *sc);

/** Host acknowledge of the current dialogue (Z pressed). */
void ff4_scene_intro_ack(FF4_SceneCtx *sc);

/**
 * One game frame for the scene machine. `field_dir` = the direction the
 * player wants to walk (or FF4_DIR_NONE); `battle_speed` gates ATB.
 * Returns true while a scene is active.
 */
bool ff4_scene_frame(const FF4_AssetDB *db, FF4_GameState *g,
                     FF4_SceneCtx *sc, FF4_Direction field_dir);

#ifdef __cplusplus
}
#endif

#endif /* FF4_SCENE_MACHINE_H */