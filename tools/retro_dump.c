/**
 * retro_dump.c - minimal libretro frontend that boots a ROM headlessly
 * and dumps framebuffers as PNGs.
 *
 * Lets us verify rendered screens (the FF4 title etc.) against ground
 * truth from a real core (snes9x) without any display.
 *
 * Usage:
 *   retro_dump <core.so> <rom> <outdir> <frames> [every]
 *   e.g. tools/retro_dump /usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so \
 *                       third_party/ff4/rom/ff4-en1.sfc /tmp/rames 900 30
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <zlib.h>

#include "libretro.h"

static const void *framebuf;
static unsigned fb_w, fb_h;
static enum retro_pixel_format fb_fmt;
static int g_frame;
static int g_press_start_at = -1;
static int g_press_extra = -1;   /* FF4_PRESSBTN: extra/alternate button id */   /* pulse Start at this frame */
static int g_press_to = -1;      /* FF4_PRESS_TO: stop the periodic presses here */
static int g_press_start_len = 12;
static int g_press_every = 0;
static int g_verbose_input = 0;
static int g_hold_dir = -1;      /* RETRO joypad id to hold from g_hold_at */
static int g_hold_at = -1;
static int g_hold_len = 0;       /* pulse Start every N frames */
static int env_ok;

static bool retro_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;   /* no logger attached */
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        fb_fmt = *(const enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        return true;
    default:
        return false;
    }
}

static uint32_t fb_copy[512 * 512];

static void retro_video_refresh(const void *data, unsigned width,
                                unsigned height, size_t pitch)
{
    if (!data || width > 512 || height > 512)
        return;

    /* The core hands us a pointer to ITS buffer (reused next frame) in
     * RETRO_PIXEL_FORMAT_RGB565 (2 bytes/px) -- but write_png() wants one
     * packed 0x00RRGGBB word per pixel.  Not converting (and not copying)
     * here is what produced the fixed "garbage" frame that poisoned every
     * reference capture: the 16-bit buffer was read as 32-bit pixels. */
    if (fb_fmt == RETRO_PIXEL_FORMAT_RGB565) {
        for (unsigned y = 0; y < height; ++y) {
            const uint16_t *row =
                (const uint16_t *)((const unsigned char *)data + y * pitch);
            for (unsigned x = 0; x < width; ++x) {
                unsigned c = row[x];
                unsigned r = (c >> 11) & 0x1F;
                unsigned g = (c >> 5) & 0x3F;
                unsigned b = c & 0x1F;
                r = (r << 3) | (r >> 2);
                g = (g << 2) | (g >> 4);
                b = (b << 3) | (b >> 2);
                fb_copy[y * width + x] = (r << 16) | (g << 8) | b;
            }
        }
    } else {
        for (unsigned y = 0; y < height; ++y) {
            const uint32_t *row =
                (const uint32_t *)((const unsigned char *)data + y * pitch);
            for (unsigned x = 0; x < width; ++x)
                fb_copy[y * width + x] = row[x] & 0xFFFFFFu;
        }
    }
    framebuf = fb_copy;
    fb_w = width;
    fb_h = height;
}

static void retro_audio_sample(int16_t l, int16_t r)
{
    (void)l;
    (void)r;
}

static size_t retro_audio_sample_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void retro_input_poll(void) { }
static int16_t retro_input_state(unsigned port, unsigned device,
                                 unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    if (g_verbose_input && id == RETRO_DEVICE_ID_JOYPAD_START) {
        static int first = 1;
        if (first) { fprintf(stderr, "[input_state called, frame=%d]\n", g_frame); first = 0; }
    }
    /* hold a direction for g_hold_len frames from g_hold_at */
    if (g_hold_dir >= 0 && id == g_hold_dir && g_hold_at >= 0 &&
        g_frame >= g_hold_at &&
        (g_hold_len <= 0 || g_frame < g_hold_at + g_hold_len))
        return 1;
    if ((id == RETRO_DEVICE_ID_JOYPAD_START ||
         id == RETRO_DEVICE_ID_JOYPAD_A ||
         id == g_press_extra) &&
        ((g_press_start_at >= 0 &&
          g_frame >= g_press_start_at &&
          (g_press_to < 0 || g_frame < g_press_to) &&
          g_frame < g_press_start_at + g_press_start_len) ||
         (g_press_every > 0 &&
          g_frame % g_press_every < g_press_start_len)))
        {
        if (g_verbose_input)
            fprintf(stderr, "[press read at frame=%d id=%d]\n", g_frame, id);
        return 1;
    }
    return 0;
}

/* minimal PNG writer (filter 0) */
static unsigned crc_tab[256];
static void crc_init(void)
{
    for (unsigned n = 0; n < 256; ++n) {
        unsigned c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static unsigned png_crc32(const unsigned char *p, size_t n)
{
    unsigned c = ~0u;
    for (size_t i = 0; i < n; ++i)
        c = crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
static void png_chunk(FILE *f, const char *type, const void *payload,
                      size_t len)
{
    unsigned char hdr[8];
    const unsigned char typeb[4] = {(unsigned char)type[0],
                                    (unsigned char)type[1],
                                    (unsigned char)type[2],
                                    (unsigned char)type[3]};
    hdr[0] = (len >> 24) & 0xFF;
    hdr[1] = (len >> 16) & 0xFF;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    fwrite(hdr, 1, 4, f);
    fwrite(typeb, 1, 4, f);
    fwrite(payload, 1, len, f);
    {
        unsigned char *all = malloc(4 + len);
        unsigned crc;

        memcpy(all, typeb, 4);
        memcpy(all + 4, payload, len);
        crc = png_crc32(all, 4 + len);
        hdr[0] = (crc >> 24) & 0xFF;
        hdr[1] = (crc >> 16) & 0xFF;
        hdr[2] = (crc >> 8) & 0xFF;
        hdr[3] = crc & 0xFF;
        fwrite(hdr, 1, 4, f);
        free(all);
    }
}

static int write_png(const char *path, unsigned w, unsigned h,
                     const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");
    unsigned char sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    unsigned char ihdr[13];
    unsigned char *raw, *fbuf;
    unsigned long rawlen, idatlen;
    size_t sz;

    if (!f)
        return -1;
    fwrite(sig, 1, 8, f);

    ihdr[0] = (w >> 24) & 0xFF;
    ihdr[1] = (w >> 16) & 0xFF;
    ihdr[2] = (w >> 8) & 0xFF;
    ihdr[3] = w & 0xFF;
    ihdr[4] = (h >> 24) & 0xFF;
    ihdr[5] = (h >> 16) & 0xFF;
    ihdr[6] = (h >> 8) & 0xFF;
    ihdr[7] = h & 0xFF;
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 6;   /* RGBA */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    rawlen = (unsigned long)h * (1 + (unsigned long)w * 4);
    raw = malloc(rawlen);
    for (unsigned y = 0; y < h; ++y) {
        raw[y * (1 + (size_t)w * 4)] = 0;
        for (unsigned x = 0; x < (unsigned)w * 4; ++x) {
            uint32_t px = pixels[(size_t)y * w + x / 4];
            unsigned char v;
            switch (x & 3) {
            case 0: v = (px >> 16) & 0xFF; break;
            case 1: v = (px >> 8) & 0xFF; break;
            case 2: v = px & 0xFF; break;
            default: v = 0xFF; break;
            }
            raw[y * (1 + (size_t)w * 4) + 1 + x] = v;
        }
    }

    fbuf = (unsigned char *)malloc(compressBound(rawlen));
    {
        uLong bound = compressBound(rawlen);

        compress2(fbuf, &bound, raw, rawlen, 9);
        sz = (size_t)bound;
    }
    png_chunk(f, "IDAT", fbuf, sz);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(fbuf);
    return 0;
}

int main(int argc, char **argv)
{
    void *core;
    void (*r_init)(void);
    void (*r_deinit)(void);
    bool (*r_load)(const struct retro_game_info *);
    void (*r_run)(void);
    void *(*r_mem)(unsigned);
    void (*r_setcport)(unsigned, unsigned);
    void (*r_reset)(void);
    bool (*r_setpix)(enum retro_pixel_format);
    void (*f_set_env)(retro_environment_t);
    void (*f_set_video)(retro_video_refresh_t);
    void (*f_set_audio)(retro_audio_sample_t);
    void (*f_set_audio_batch)(retro_audio_sample_batch_t);
    void (*f_set_input_poll)(retro_input_poll_t);
    void (*f_set_input_state)(retro_input_state_t);
    struct retro_game_info info;
    void *f;
    int frames, every;
    char path[512];
    unsigned long begin, end;
    unsigned char *rom;
    long sz;

    if (argc < 5) {
        fprintf(stderr, "usage: %s core.so rom outdir frames [every]\n",
                argv[0]);
        return 1;
    }

    /* load the ROM */
    f = fopen(argv[2], "rb");
    if (!f) {
        perror(argv[2]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) {
        fclose(f);
        return 1;
    }
    fclose(f);

    core = dlopen(argv[1], RTLD_NOW);
    if (!core) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    r_init = (void (*)(void))dlsym(core, "retro_init");
    r_deinit = (void (*)(void))dlsym(core, "retro_deinit");
    r_load = (bool (*)(const struct retro_game_info *))dlsym(core,
                                                              "retro_load_game");
    r_run = (void (*)(void))dlsym(core, "retro_run");
    r_mem = (void *(*)(unsigned))dlsym(core, "retro_get_memory_data");
    r_setcport = (void (*)(unsigned, unsigned))dlsym(
        core, "retro_set_controller_port_device");
    r_reset = (void (*)(void))dlsym(core, "retro_reset");
    r_setpix = (bool (*)(enum retro_pixel_format))dlsym(core,
                                                    "retro_set_pixel_format");
    if (!r_init || !r_load || !r_run) {
        fprintf(stderr, "core symbols missing\n");
        return 1;
    }

    f_set_env = (void (*)(retro_environment_t))dlsym(core,
                                                     "retro_set_environment");
    f_set_video = (void (*)(retro_video_refresh_t))dlsym(
        core, "retro_set_video_refresh");
    f_set_audio = (void (*)(retro_audio_sample_t))dlsym(
        core, "retro_set_audio_sample");
    f_set_audio_batch = (void (*)(retro_audio_sample_batch_t))dlsym(
        core, "retro_set_audio_sample_batch");
    f_set_input_poll = (void (*)(retro_input_poll_t))dlsym(
        core, "retro_set_input_poll");
    f_set_input_state = (void (*)(retro_input_state_t))dlsym(
        core, "retro_set_input_state");
    if (!f_set_env || !f_set_video || !f_set_input_poll) {
        fprintf(stderr, "callback setters missing\n");
        return 1;
    }
    f_set_env(retro_environment);
    f_set_video(retro_video_refresh);
    if (f_set_audio)
        f_set_audio(retro_audio_sample);
    if (f_set_audio_batch)
        f_set_audio_batch(retro_audio_sample_batch);
    f_set_input_poll(retro_input_poll);
    f_set_input_state(retro_input_state);

    fprintf(stderr, "[init]\n");
    r_init();
    /* without this the core has no joypad plugged in and every button reads
     * as 0 -- which is why movement tests never moved anything */
    if (r_setcport)
        r_setcport(0, 1);            /* port 0, RETRO_DEVICE_JOYPAD */
    fprintf(stderr, "[pixfmt]\n");
    if (r_setpix)
        r_setpix(RETRO_PIXEL_FORMAT_XRGB8888);
    else
        fprintf(stderr, "[no pixfmt setter]\n");
    fprintf(stderr, "[load]\n");
    memset(&info, 0, sizeof info);
    info.path = argv[2];
    info.data = rom;
    info.size = sz;
    if (!r_load(&info)) {
        fprintf(stderr, "load_game failed\n");
        return 1;
    }

    frames = atoi(argv[4]);
    every = argc > 5 ? atoi(argv[5]) : 1;
    crc_init();

    if (argc > 6)
        g_press_start_at = atoi(argv[6]);
    if (argc > 7)
        g_press_every = atoi(argv[7]);
    if (argc > 8)
        g_verbose_input = atoi(argv[8]);
    { const char *pb = getenv("FF4_PRESSBTN");

      if (pb)
          g_press_extra = atoi(pb); }
    { const char *pt = getenv("FF4_PRESS_TO");

      if (pt)
          g_press_to = atoi(pt); }
    /* args: holdAt holdLen holdButton (5=down,4=up,6=left,7=right) */
    if (argc > 9)
        g_hold_at = atoi(argv[9]);
    if (argc > 10)
        g_hold_len = atoi(argv[10]);
    if (argc > 11)
        g_hold_dir = atoi(argv[11]);
    /* FF4_VRAM_EVERY=n FF4_VRAM_FROM=a FF4_VRAM_TO=b: dump VRAM every n frames
     * inside [a,b) to FF4_VRAM_OUT + "_%04d.bin".  Dense sampling of one run,
     * for animation cadence work (cf. FF4_VRAM, a single dump at the end). */
    const char *vevery = getenv("FF4_VRAM_EVERY");
    const char *vout = getenv("FF4_VRAM_OUT");
    int vfrom = getenv("FF4_VRAM_FROM") ? atoi(getenv("FF4_VRAM_FROM")) : 0;
    int vto = getenv("FF4_VRAM_TO") ? atoi(getenv("FF4_VRAM_TO")) : frames;

    /* FF4_RAM_EVERY=n FF4_RAM_FROM=a FF4_RAM_TO=b FF4_RAM_OUT=p: dense
     * WRAM sampling for animation-mechanism proofs (cf. FF4_VRAM_EVERY). */
    const char *revery = getenv("FF4_RAM_EVERY");
    const char *rout = getenv("FF4_RAM_OUT");
    int rfrom = getenv("FF4_RAM_FROM") ? atoi(getenv("FF4_RAM_FROM")) : 0;
    int rto = getenv("FF4_RAM_TO") ? atoi(getenv("FF4_RAM_TO")) : frames;

    for (int i = 0; i < frames; ++i) {
        g_frame = i;
        r_run();
        if (revery && rout && r_mem && i >= rfrom && i < rto &&
            (i % atoi(revery)) == 0) {
            const unsigned char *ram =
                (const unsigned char *)r_mem(2);      /* SYSTEM_RAM */
            const char *raddr = getenv("FF4_RAM_ADDR");
            unsigned rlo = 0, rlen = 0x200;

            if (raddr)
                sscanf(raddr, "%x:%x", &rlo, &rlen);
            if (ram) {
                char rp[600];
                FILE *rf;

                snprintf(rp, sizeof rp, "%s_%04d.bin", rout, i);
                rf = fopen(rp, "wb");
                if (rf) {
                    fwrite(ram + rlo, 1, rlen, rf);
                    fclose(rf);
                }
            }
        }
        if (vevery && vout && r_mem && i >= vfrom && i < vto &&
            (i % atoi(vevery)) == 0) {
            const unsigned char *vr =
                (const unsigned char *)r_mem(RETRO_MEMORY_VIDEO_RAM);

            if (vr) {
                char vp[600];
                FILE *vf;

                snprintf(vp, sizeof vp, "%s_%04d.bin", vout, i);
                vf = fopen(vp, "wb");
                if (vf) {
                    fwrite(vr, 1, 0x10000, vf);
                    fclose(vf);
                }
            }
        }
        if (fb_w && fb_h && (i % every) == 0) {
            snprintf(path, sizeof path, "%s/frame_%04d.png", argv[3], i);
            write_png(path, fb_w, fb_h, (const uint32_t *)framebuf);
        }
    }

    /* FF4_VRAM=0x1200:0x200 -> binary dump of VRAM, for verifying the tile
     * animation source (TfrBGAnimGfx uploads phases to VRAM $1200). */
    if (getenv("FF4_VRAM") && r_mem) {
        unsigned addr = 0, len = 0x200;
        const unsigned char *vram =
            (const unsigned char *)r_mem(RETRO_MEMORY_VIDEO_RAM);
        const char *out = getenv("FF4_VRAM_OUT");

        sscanf(getenv("FF4_VRAM"), "%x:%x", &addr, &len);
        if (vram && len && addr + len <= 0x10000 && out) {
            FILE *f = fopen(out, "wb");

            if (f) {
                fwrite(vram + addr, 1, len, f);
                fclose(f);
                fprintf(stderr, "[vram] wrote %s (%04X+%X)\n", out, addr, len);
            }
        }
    }

    /* FF4_RAM=0x06a0:32 -> hex dump of WRAM at the end of the run.  Used to
     * read the game's own derived per-direction tile-property table ($06a1),
     * which is what CheckTilePass actually tests. */
    if (getenv("FF4_RAM") && r_mem) {
        unsigned addr = 0, len = 16;
        unsigned char *ram = (unsigned char *)r_mem(2);   /* SYSTEM_RAM */

        sscanf(getenv("FF4_RAM"), "%x:%u", &addr, &len);
        if (ram && len && addr + len <= 0x20000) {
            unsigned i;

            fprintf(stderr, "[ram] %04X:", addr);
            for (i = 0; i < len; ++i)
                fprintf(stderr, "%s%02X", (i % 16) ? " " : "\n[ram] ",
                        ram[addr + i]);
            fprintf(stderr, "\n");
        }
    }

    r_deinit();
    dlclose(core);
    free(rom);
    printf("done: %d frames, %ux%u, fmt %d\n", frames, fb_w, fb_h, fb_fmt);
    return 0;
}