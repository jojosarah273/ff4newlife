/**
 * @file menu_engine.h
 * @brief Phase 7: UI & menu engine.
 *
 * Cursor-driven menu state machines bound to the game state: the main
 * menu (Item/Magic/Equip/Status/Order/Config/Save/Exit), inventory with
 * paging, magic tabs per character, equipping (job + slot filtering),
 * status, the battle command list (per-class commands), battle target
 * selection, shops, and confirmation prompts.
 *
 * Rendering is platform-side: the engine maintains rows/text/cursor
 * geometry (the SNES windows/listing model in spirit: a page of rows,
 * absolute cursor, scroll offset) that renderers draw. Text comes from
 * the db tables and ff4_text_format() resolves the runtime tokens
 * (\\item, \\gp, \\pad...).
 */
#ifndef FF4_MENU_ENGINE_H
#define FF4_MENU_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ff4/core/ff4_types.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  List model                                                          */
/* ==================================================================== */

#define FF4_MENU_MAX_ROWS 24
#define FF4_MENU_PAGE_ITEMS 9      /**< Inventory page (game shows 9). */

typedef struct FF4_MenuRow {
    const char *text;              /**< Display text (may be static). */
    int value;                     /**< Item/spell/command id behind the row. */
    bool enabled;
} FF4_MenuRow;

typedef struct FF4_MenuList {
    FF4_MenuRow rows[FF4_MENU_MAX_ROWS];
    char arena[FF4_MENU_MAX_ROWS][32]; /**< Builder-owned row text. */
    int count;
    int cursor;                    /**< Absolute row index. */
    int top;                       /**< First visible row. */
    int page;                      /**< Visible rows (window height). */
    const char *title;
    char title_buf[24];
} FF4_MenuList;

/** Menu screens. */
typedef enum FF4_MenuScreen {
    FF4_MENU_NONE = 0,
    FF4_MENU_MAIN,                 /**< Item/Magic/Equip/Status/Order/... */
    FF4_MENU_ITEM,                 /**< Inventory list. */
    FF4_MENU_MAGIC,                /**< White/Black/Call tabs. */
    FF4_MENU_EQUIP,                /**< Equipment slots -> candidates. */
    FF4_MENU_STATUS,               /**< Party member stat screen. */
    FF4_MENU_ORDER,                /**< Row/order setting. */
    FF4_MENU_CONFIG,               /**< Config toggles. */
    FF4_MENU_BATTLE_CMD,           /**< Battle command select. */
    FF4_MENU_TARGET,               /**< Battle target grid. */
    FF4_MENU_SHOP,                 /**< Shop (buy/sell lists). */
    FF4_MENU_SELECT,               /**< Generic single-select prompt. */
    FF4_MENU_CONFIRM               /**< Yes/no prompt. */
} FF4_MenuScreen;

/** One interactive menu session. */
typedef struct FF4_MenuCtx {
    FF4_MenuScreen screen;
    FF4_MenuList list;
    int party_index;               /**< Selected character (0-4). */
    int magic_tab;                 /**< 0 white / 1 black / 2 call. */
    int shop_mode;                 /**< 0 buy / 1 sell / 2 exit. */
    int value;                     /**< Pending payload (item id etc). */
    bool open;
} FF4_MenuCtx;

/* ==================================================================== */
/*  Input / navigation                                                  */
/* ==================================================================== */

typedef enum FF4_MenuInput {
    FF4_MENU_IN_UP = 0,
    FF4_MENU_IN_DOWN,
    FF4_MENU_IN_LEFT,
    FF4_MENU_IN_RIGHT,
    FF4_MENU_IN_CONFIRM,
    FF4_MENU_IN_CANCEL
} FF4_MenuInput;

typedef struct FF4_MenuResult {
    bool changed;                  /**< The menu repainted. */
    bool done;                     /**< The screen closed/quit. */
    int value;                     /**< Confirmed selection (if any). */
} FF4_MenuResult;

/** Open a screen: builds its rows and positions the cursor. */
void ff4_menu_open(const FF4_AssetDB *db, FF4_GameState *g, FF4_MenuCtx *m,
                   FF4_MenuScreen screen);

/** Feed one input; returns the result + side effects on game state. */
FF4_MenuResult ff4_menu_input(const FF4_AssetDB *db, FF4_GameState *g,
                              FF4_MenuCtx *m, FF4_MenuInput input);

/* ==================================================================== */
/*  Row builders (also usable directly)                                 */
/* ==================================================================== */

void ff4_menu_build_main(FF4_MenuList *l);
void ff4_menu_build_inventory(const FF4_AssetDB *db, const FF4_GameState *g,
                              FF4_MenuList *l);
void ff4_menu_build_magic(const FF4_AssetDB *db, const FF4_GameState *g,
                          FF4_MenuList *l, int party_index, int tab);
void ff4_menu_build_equip(const FF4_AssetDB *db, const FF4_GameState *g,
                          FF4_MenuList *l, int party_index, int slot);
void ff4_menu_build_battle_cmds(const FF4_AssetDB *db, FF4_MenuList *l,
                                const FF4_PartyMember *m);
void ff4_menu_build_shop(const FF4_AssetDB *db, FF4_MenuList *l, int shop,
                         int mode);
void ff4_menu_build_confirm(FF4_MenuList *l, const char *prompt);

/* ==================================================================== */
/*  Text formatting                                                     */
/* ==================================================================== */

/**
 * Resolve a rip string into display text: handles the \\item / \\gp /
 * \\pad tokens plus newlines. out must hold at least `cap` bytes.
 */
size_t ff4_text_format(const FF4_AssetDB *db, const FF4_GameState *g,
                       const char *src, char *out, size_t cap);

/** Item name with quantity (the "Potion x5" pattern). */
size_t ff4_text_item_row(const FF4_AssetDB *db, uint8_t item, uint8_t qty,
                       char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FF4_MENU_ENGINE_H */