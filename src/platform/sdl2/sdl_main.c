/**
 * @file sdl_main.c
 * @brief SDL2 host: the interactive scene runtime (testable demo).
 *
 * 1200x700 window (logical 360x224). Controls:
 *   arrows  walk           Z  confirm / accept
 *   Enter   main menu      X  cancel
 *   B       force battle   Q/Esc quit
 *
 * The field grid colors: green = passable, dark red = blocked, yellow =
 * the party. Battles auto-fight to their conclusion; the scene machine
 * returns to the field with rewards. The battle/command window at the
 * bottom shows the last events.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#include <SDL.h>

#include "ff4/core/ff4_gfx.h"
#include "ff4/core/ff4_state.h"
#include "ff4/engine/asset_db.h"
#include "ff4/engine/audio_engine.h"
#include "ff4/engine/field_engine.h"
#include "ff4/engine/menu_engine.h"
#include "ff4/engine/scene_machine.h"
#include "ff4/platform/audio_api.h"
#include "ff4/platform/app_paths.h"
#include "ff4/platform/render_api.h"

static void render_field(const FF4_AssetDB *db, const FF4_GameState *g)
{
    int x, y, px, py;
    char line[80];

    /* debug grid: center the camera on the party (360x224 viewport) */
    px = (int)g->world.x * 16 - 172;
    py = (int)g->world.y * 16 - 104;
    for (y = 0; y < 14; ++y)
        for (x = 0; x < 23; ++x) {
            int tx = x * 16 - px / 16 + (px < 0 ? 0 : 0);
            int ty = y * 16 - py / 16;
            uint8_t b1, b2;

            tx = (int)g->world.x - (360 / 32) + x + 1;
            ty = (int)g->world.y - (224 / 32) + y + 1;
            if (tx < 0 || ty < 0)
                continue;
            ff4_field_tile_props(db, g, tx, ty, &b1, &b2);
            if (ff4_field_tile_pass_bytes(b1, b1, (uint8_t)g->world.z))
                ff4_render_rect(x * 16, y * 16, 15, 15, 0x22, 0x3C, 0x22);
            else if (b2 & (FF4_TP2_BATTLE | FF4_TP2_TRIGGER | FF4_TP2_EXIT))
                ff4_render_rect(x * 16, y * 16, 15, 15, 0x40, 0x40, 0x10);
            else
                ff4_render_rect(x * 16, y * 16, 15, 15, 0x28, 0x1E, 0x20);
        }
    /* the party (camera-centred) */
    {
        int sx = (int)g->world.x * 16 - ((int)g->world.x * 16 - 172) - 178;

        /* party drawn near the viewport centre */
        ff4_render_rect(360 / 2 - 8, 224 / 2 - 8, 16, 16, 0xFF, 0xFF, 0x20);
        (void)sx;
    }

    snprintf(line, sizeof line,
             "Map %u  pos %u,%u  facing %d  battle %s",
             g->world.map_id, g->world.x, g->world.y, (int)g->world.facing,
             g->world.battle_enabled ? "on" : "off");
    ff4_render_text(4, 4, line, 0xE0, 0xE0, 0xE0);
    for (int i = 0; i < FF4_PARTY_MAX; ++i) {
        const FF4_PartyMember *m = &g->party[i];

        if (m->id == FF4_CHAR_NONE)
            continue;
        snprintf(line, sizeof line, "P%d HP %u/%u MP %u/%u", i, m->hp,
                 m->max_hp, m->mp, m->max_mp);
        ff4_render_text(4, 196 - i * 10, line, 0x80, 0xE0, 0x80);
    }
}

static void render_battle(const FF4_AssetDB *db, const FF4_GameState *g)
{
    char line[64];

    ff4_render_text(4, 4, "BATTLE (auto)", 0xFF, 0x80, 0x80);
    for (int i = 0; i < 3; ++i) {
        const FF4_Monster *mo = &g->battle.monsters[i];
        int slot = FF4_PARTY_MAX + i;

        if (!g->battle.present[slot])
            continue;
        snprintf(line, sizeof line, "E%d %s HP %u", i,
                 mo->core.id < FF4_DB_MONSTERS
                     ? db->monsters[mo->core.id].name
                     : "?",
                 mo->core.hp);
        ff4_render_text(8, 24 + i * 10, line, 0xE0, 0x90, 0x90);
    }
    for (int i = 0; i < FF4_PARTY_MAX; ++i) {
        const FF4_PartyMember *m = &g->battle.party[i];

        if (m->id == FF4_CHAR_NONE)
            continue;
        if (g->battle.present[i]) {
            snprintf(line, sizeof line, "P%d HP %u/%u", i, m->hp, m->max_hp);
            ff4_render_text(8, 70 + i * 10, line, 0x80, 0xE0, 0x80);
        } else {
            snprintf(line, sizeof line, "P%d KO", i);
            ff4_render_text(8, 70 + i * 10, line, 0x80, 0x80, 0x80);
        }
    }
}

static void render_menu(const FF4_MenuCtx *m)
{
    int i;

    ff4_render_text(4, 4, "MAIN MENU  (Z ok / X back / arrows move)", 0xC0,
                    0xC0, 0xFF);
    for (i = 0; i < m->list.count && i < m->list.page; ++i) {
        const FF4_MenuRow *r = &m->list.rows[i];

        if (m->list.cursor == i)
            ff4_render_text(8, 20 + i * 12, "> ", 0xFF, 0xFF, 0x60);
        if (r->text)
            ff4_render_text(24, 20 + i * 12, r->text,
                            m->list.cursor == i ? 0xFF : 0xC0,
                            m->list.cursor == i ? 0xFF : 0xC0,
                            m->list.cursor == i ? 0x60 : 0xC0);
    }
}

/* ---- headless frame dumps (FF4_DUMP=1): write the intro view as PNG ---- */
static unsigned g_dump_crc[256];
static void dump_crc_init(void)
{
    unsigned n;
    for (n = 0; n < 256; ++n) {
        unsigned c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_dump_crc[n] = c;
    }
}
static unsigned png_crc(const unsigned char *p, size_t n)
{
    unsigned c = ~0u;
    for (size_t i = 0; i < n; ++i)
        c = g_dump_crc[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
static void put_be32(unsigned char *dst, unsigned long v)
{
    dst[0] = (unsigned char)(v >> 24);
    dst[1] = (unsigned char)(v >> 16);
    dst[2] = (unsigned char)(v >> 8);
    dst[3] = (unsigned char)v;
}
static void png_write(const char *path, int w, int h, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    size_t pitch = (size_t)w * 4;
    size_t rlen = pitch * (size_t)h + (size_t)h;
    unsigned char *raw, *idat;
    size_t pos = 0, off = 0, idlen;
    unsigned char head[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    unsigned char ihdr[33];
    int y;

    if (!f)
        return;
    /* ---- IHDR (25 bytes) ---- */
    memset(ihdr, 0, sizeof ihdr);
    put_be32(ihdr, 13);                 /* chunk length */
    memcpy(ihdr + 4, "IHDR", 4);
    put_be32(ihdr + 8, (unsigned)w);
    put_be32(ihdr + 12, (unsigned)h);
    ihdr[16] = 8; ihdr[17] = 6;         /* 8-bit RGBA, deflate */
    put_be32(ihdr + 21, png_crc(ihdr + 4, 17));

    /* ---- IDAT ---- */
    raw = (unsigned char *)malloc(rlen);
    for (y = 0; y < h; ++y) {
        raw[(size_t)y * (pitch + 1)] = 0;
        memcpy(raw + (size_t)y * (pitch + 1) + 1,
               (const unsigned char *)px + (size_t)y * pitch, pitch);
    }
    idlen = rlen + ((rlen / 65535) + 1) * 5 + 6;
    idat = (unsigned char *)malloc(idlen);
    idat[pos++] = 0x78;                 /* zlib: deflate, 32K window */
    idat[pos++] = 0x01;
    while (off < rlen) {                /* stored (uncompressed) blocks */
        size_t n = rlen - off;
        unsigned char hdr5[5];

        if (n > 65535)
            n = 65535;
        hdr5[0] = (unsigned char)((off + n >= rlen) ? 1 : 0);
        hdr5[1] = (unsigned char)(n & 0xFF);
        hdr5[2] = (unsigned char)(n >> 8);
        hdr5[3] = (unsigned char)(~n & 0xFF);
        hdr5[4] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(idat + pos, hdr5, 5);
        pos += 5;
        memcpy(idat + pos, raw + off, n);
        pos += n;
        off += n;
    }
    {
        unsigned long a1 = 1, a2 = 0;
        size_t i;

        for (i = 0; i < rlen; ++i) {
            a1 = (a1 + raw[i]) % 65521u;
            a2 = (a2 + a1) % 65521u;
        }
        put_be32(idat + pos, ((a2 << 16) | a1));
        pos += 4;
    }

    fwrite(head, 1, 8, f);
    fwrite(ihdr, 1, 25, f);
    {
        unsigned char ch[12];
        unsigned c;

        put_be32(ch, (unsigned long)pos);
        memcpy(ch + 4, "IDAT", 4);
        fwrite(ch, 1, 8, f);
        fwrite(idat, 1, pos, f);
        c = png_crc(ch + 4, 4);
        put_be32(ch, c);
        fwrite(ch, 1, 4, f);

        put_be32(ch, 0);
        memcpy(ch + 4, "IEND", 4);
        fwrite(ch, 1, 8, f);
        c = png_crc(ch + 4, 4);
        put_be32(ch, c);
        fwrite(ch, 1, 4, f);
    }
    free(raw);
    free(idat);
    fclose(f);
}
static int g_dump_every = 30, g_dump_cnt = 0;
static void dump_view(const FF4_GfxSurface *s, int idxbase)
{
    const char *dir = getenv("FF4_DUMP");
    char path[512];
    static unsigned inited = 0;

    if (!dir)
        return;
    if (!inited) {
        dump_crc_init();
        inited = 1;
    }
    if (g_dump_cnt % g_dump_every != 0) {
        ++g_dump_cnt;
        return;
    }
    snprintf(path, sizeof path, "%s/intro_%06d.png", dir, idxbase + g_dump_cnt);
    if (s && s->pixels) {
        int i2, nz2 = 0;
        for (i2 = 0; i2 < s->w * s->h; ++i2)
            if (s->pixels[i2] & 0xFFFFFF)
                ++nz2;
        fprintf(stderr, "[dump %s] nz=%d\n", path, nz2);
        png_write(path, s->w, s->h, s->pixels);
    }
    ++g_dump_cnt;
}

/* The US (FF2) fresh-run title is a pure black screen while the
   Prelude plays (verified against snes9x): show that until Z. */
static void render_opening(const FF4_SceneCtx *sc, const char *gfx_path)
{
    (void)gfx_path;
    if (sc->opening != FF4_OPENING_TITLE)
        return;
    ff4_render_rect(0, 0, 1200, 700, 0, 0, 0);
    ff4_render_text(84, 200, "Z = NEW GAME", 0xD0, 0xD0, 0xF0);
}

/* dim a surface toward black by (15 - level) / 15 */
static void dim_surface(FF4_GfxSurface *s, int level)
{
    int i, n;

    if (getenv("FF4_NODIM"))
        return;
    if (!s || !s->pixels || level >= 15)
        return;
    if (level <= 0) {
        n = s->w * s->h;
        for (i = 0; i < n; ++i)
            s->pixels[i] &= 0xFF000000u;
        return;
    }
    n = s->w * s->h;
    for (i = 0; i < n; ++i) {
        uint32_t p = s->pixels[i];
        uint32_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;

        r = r * (uint32_t)level / 15u;
        g = g * (uint32_t)level / 15u;
        b = b * (uint32_t)level / 15u;
        s->pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* draw the striped world-sprite airship drift across the sky.
   The intro camera pans east over the world while two Red Wings
   airships climb across it (matches the game's own opening). */
static void render_flyover(const FF4_SceneCtx *sc, const FF4_AssetDB *db)
{
    FF4_GfxSurface world;
    int t = sc->intro.fly_frame;
    int cx = (int)((sc->intro.cam_px + t * 2) ) % 256;
    int cy = 96;                     /* the worldbelt the intro flies over */

    if (ff4_gfx_render_world(db, sc->intro.world, cx, cy, 32, 28,
                             &world) != 0)
        return;
    {
        int dx = (48 + t * 3) % 200, dy = (int)(224 - (t % 224));
        int dx2 = (int)(160 + (t * 2) % 140), dy2 = (int)(190 - (t % 190));

        ff4_gfx_draw_world_sprite(&world, dx, dy, db, 0, 0, 0);
        ff4_gfx_draw_world_sprite(&world, dx2, dy2, db, 1, 0, 0);
    }
    dim_surface(&world, sc->intro.fade);
    dump_view(&world, 10000);
    ff4_render_blit(&world, 0, 0, 3);
    ff4_gfx_surface_free(&world);
}

static void render_intro(const FF4_SceneCtx *sc, const FF4_AssetDB *db)
{
    FF4_GfxSurface view;
    bool ok = false;
    clock_t _t0 = clock();

    if (getenv("FF4_TRACE"))
        fprintf(stderr, "[r-intro start]\n");
    if (sc->intro.map < 0) {
        render_flyover(sc, db);
    } else {
        if (getenv("FF4_TRACE")) {
            clock_t t0 = clock();
            ok = ff4_gfx_render_submap(db, sc->intro.map,
                                       sc->intro.cam_x, sc->intro.cam_y, 32, 28,
                                       true, &view) == 0;
            clock_t t1 = clock();
            {
                int i2, nz2 = 0;
                for (i2 = 0; i2 < view.w * view.h; ++i2)
                    if (view.pixels[i2] & 0xFFFFFF)
                        ++nz2;
                fprintf(stderr, "[render-submap %d] %ldms nz=%d cam=%d,%d\n",
                        sc->intro.map, (long)((t1 - t0) * 1000 / CLOCKS_PER_SEC),
                        nz2, sc->intro.cam_x, sc->intro.cam_y);
            }
        } else {
            ok = ff4_gfx_render_submap(db, sc->intro.map,
                                       sc->intro.cam_x, sc->intro.cam_y, 32, 28,
                                       true, &view) == 0;
        }
        if (ok) {
            int i;

            for (i = 0; i < sc->intro.actor_count; ++i) {
                const FF4_IntroActor *a = &sc->intro.actor[i];
                int sx, sy;

                if (!a->visible || a->sprite < 0)
                    continue;
                sx = a->x * 8 - sc->intro.cam_x * 8;
                sy = a->y * 8 - sc->intro.cam_y * 8 - 16;
                ff4_gfx_draw_field_sprite(&view, sx, sy, db, a->sprite,
                                          a->palette, a->step & 1);
            }
            dim_surface(&view, sc->intro.fade);
            if (getenv("FF4_TRACE")) {
                int i2, nz2 = 0;
                for (i2 = 0; i2 < view.w * view.h; ++i2)
                    if (view.pixels[i2] & 0xFFFFFF)
                        ++nz2;
                fprintf(stderr, "[view-final] nz=%d fade=%d\n", nz2,
                        sc->intro.fade);
            }
            dump_view(&view, 20000);
            ff4_render_blit(&view, 0, 0, 3);
            ff4_gfx_surface_free(&view);
        } else {
            ff4_render_text(4, 4, "intro: missing map?", 0xFF, 0xFF, 0xFF);
        }
    }
    if (sc->intro.dialog_open) {
        /* bottom dialogue window */
        ff4_render_rect(4, 340, 1192, 210, 8, 16, 40);
        ff4_render_rect(10, 346, 1180, 198, 200, 216, 255);
        ff4_render_text(22, 356, "[Z]", 40, 60, 160);
        ff4_render_text(24, 368, sc->intro.dialog_text,
                        0, 0, 0);
    }
    if (sc->intro.flash > 0)
        ff4_render_rect(0, 0, 1200, 700, 240, 240, 255);
    if (getenv("FF4_TRACE")) {
        clock_t _t1 = clock();
        fprintf(stderr, "[render-intro-total] %ldms\n",
                (long)((_t1 - _t0) * 1000 / CLOCKS_PER_SEC));
    }
}


static void render_log(const FF4_SceneCtx *sc)
{
    int i, start = sc->log_count >= 3 ? sc->log_count - 3 : 0;

    ff4_render_text(4, 196, "-- event log --", 0x60, 0x80, 0x60);
    for (i = 0; i < sc->log_count - start && i < 4; ++i)
        ff4_render_text(4, 206 + i * 10 - (start ? 10 : 0) + 0,
                        sc->log[start + i], 0x90, 0xB0, 0x90);
}

static void sdl_audio_song(void *o, int t) { (void)o; ff4_audio_queue_song(t); }
static void sdl_audio_sfx(void *o, int s)  { (void)o; ff4_audio_queue_sfx(s); }
static void sdl_audio_nop(void *o)         { (void)o; }
static void sdl_audio_vol(void *o, int v)  { (void)o; (void)v; }

int main(int argc, char **argv)
{
    FF4_AssetDB db;
    FF4_GameState g;
    FF4_SceneCtx sc;
    FF4_AudioDriver audio;
    FF4_AudioBackend be;
    FF4_MenuCtx menu;
    int max_frames = argc > 1 ? atoi(argv[1]) : 0;
    int frames = 0;
    bool menu_just_pressed = false;
    char data_path[512], gfx_path[512];

    if (!ff4_resolve_path("data/ff4-en-data.json", data_path,
                          sizeof data_path)) {
        fprintf(stderr, "cannot find data/ff4-en-data.json\n");
        return 1;
    }
    if (ff4_db_load(&db, data_path) != 0) {
        fprintf(stderr, "cannot load %s\n", data_path);
        return 1;
    }
    if (!ff4_resolve_path("data/graphics/title.png", gfx_path,
                          sizeof gfx_path)) {
        fprintf(stderr, "cannot find data/graphics (run tools/export_gfx.js)\n");
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (ff4_render_init(1200, 700, 0) != 0) {
        fprintf(stderr, "render init failed\n");
        return 1;
    }

    ff4_state_init(&g);
    ff4_scene_init(&sc);            /* starts at the title screen */
    ff4_party_add(&g, FF4_CHAR_CECIL_DK, 0);
    ff4_party_add(&g, FF4_CHAR_KAIN_1, 1);
    ff4_party_add(&g, FF4_CHAR_RYDIA_CHILD, 2);
    for (int i = 0; i < FF4_PARTY_MAX; ++i) {
        g.party[i].max_hp = 300;
        g.party[i].hp = 300;
        g.party[i].max_mp = 30;
        g.party[i].mp = 30;
        g.party[i].attack_power = 40;
        g.party[i].attack_mult = 2;
        g.party[i].hit_rate = 90;
        g.party[i].crit_rate = 2;
        g.party[i].crit_bonus = 20;
        g.party[i].equip[FF4_EQ_RIGHT_HAND] = 0x2B;   /* Short (katana) */
    }
    g.world.map_id = 0;                 /* Town of Baron */
    g.world.x = 10;
    g.world.y = 20;

    memset(&be, 0, sizeof be);
    be.play_song = sdl_audio_song;
    be.stop_song = sdl_audio_nop;
    be.set_volume = sdl_audio_vol;
    be.play_sfx = sdl_audio_sfx;
    be.reset = sdl_audio_nop;
    ff4_audio_driver_init(&audio, &be, NULL);
    ff4_audio_play_song(&audio, 1);
    ff4_scene_log(&sc, "Title: Z continues, arrows/Enter after prologue");

    for (;;) {
        SDL_Event ev;
        FF4_Direction dir = FF4_DIR_NONE;
        bool enter = false, z = false, x = false, b = false, quit = false;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                quit = true;
        }
        {
            const Uint8 *k = SDL_GetKeyboardState(NULL);

            if (k[SDL_SCANCODE_UP])    dir = FF4_DIR_UP;
            if (k[SDL_SCANCODE_DOWN])  dir = FF4_DIR_DOWN;
            if (k[SDL_SCANCODE_LEFT])  dir = FF4_DIR_LEFT;
            if (k[SDL_SCANCODE_RIGHT]) dir = FF4_DIR_RIGHT;
            if (k[SDL_SCANCODE_RETURN]) enter = true;
            if (k[SDL_SCANCODE_Z])       z = true;
            if (k[SDL_SCANCODE_X])       x = true;
            if (k[SDL_SCANCODE_B])       b = true;
            if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_ESCAPE]) quit = true;
        }

        ff4_render_begin();

        switch (sc.scene) {
        case FF4_SCENE_OPENING:
            if (sc.opening == FF4_OPENING_TITLE) {
                if (z || getenv("FF4_AUTO")) {
                    ff4_scene_intro_begin(&db, &g, &sc);
                    ff4_scene_frame(&db, &g, &sc, FF4_DIR_NONE);
                }
                render_opening(&sc, gfx_path);
            } else {
                const char *auto_mode = getenv("FF4_AUTO");

                if (z || (auto_mode && sc.intro.dialog_open))
                    ff4_scene_intro_ack(&sc);
                if (getenv("FF4_TRACE") && frames < 2000)
                    fprintf(stderr,
                            "[intro] f=%d op=%d map=%d dialog=%d done=%d wait=%u pc=%zu\n",
                            frames, (int)sc.intro.run.last.kind,
                            sc.intro.map, sc.intro.dialog_open,
                            sc.intro.done, sc.intro.run.wait,
                            sc.intro.run.pc);
                ff4_scene_frame(&db, &g, &sc, FF4_DIR_NONE);
                /* keep the intro running; title.png only used pre-intro */
                render_intro(&sc, &db);
            }
            break;
        case FF4_SCENE_FIELD:
            ff4_scene_frame(&db, &g, &sc, dir);
            if (b) {
                ff4_scene_request_battle(&sc, 0);
                ff4_scene_frame(&db, &g, &sc, FF4_DIR_NONE);
            } else if (enter && !menu_just_pressed) {
                ff4_menu_open(&db, &g, &menu, FF4_MENU_MAIN);
                sc.scene = FF4_SCENE_MENU;
            }
            render_field(&db, &g);
            break;
        case FF4_SCENE_BATTLE:
            if (b)
                ff4_scene_frame(&db, &g, &sc, FF4_DIR_NONE); /* step */
            ff4_scene_frame(&db, &g, &sc, FF4_DIR_NONE);
            render_battle(&db, &g);
            break;
        case FF4_SCENE_MENU:
            if (dir) {
                ff4_menu_input(&db, &g, &menu,
                               dir == FF4_DIR_UP   ? FF4_MENU_IN_UP
                             : dir == FF4_DIR_DOWN ? FF4_MENU_IN_DOWN
                             : dir == FF4_DIR_LEFT ? FF4_MENU_IN_LEFT
                                                   : FF4_MENU_IN_RIGHT);
            } else if (z) {
                ff4_menu_input(&db, &g, &menu, FF4_MENU_IN_CONFIRM);
            } else if (x || enter) {
                if ((x || enter) && !menu_just_pressed) {
                    ff4_menu_input(&db, &g, &menu, FF4_MENU_IN_CANCEL);
                    if (!menu.open)
                        sc.scene = FF4_SCENE_FIELD;
                }
            }
            render_menu(&menu);
            break;
        default:
            break;
        }
        render_log(&sc);
        ff4_audio_tick(&audio);
        ff4_render_end();
        SDL_Delay(16);
        menu_just_pressed = enter || x;
        ++frames;
        if (quit)
            break;
        if (max_frames && frames >= max_frames)
            break;
    }

    ff4_db_free(&db);
    SDL_Quit();
    return 0;
}