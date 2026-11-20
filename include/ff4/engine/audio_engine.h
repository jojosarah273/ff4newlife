/**
 * @file audio_engine.h
 * @brief Phase 8: audio driver abstraction.
 *
 * Models the game's SPC700 mail-box protocol ($1E00-$1E03: command +
 * parameters; $1E05 current song; $1E08 sync counter) at the logic
 * level. The engine decides *what* must happen (#1=fade out, #2=play
 * song, #3=fade in...) exactly like the SNES driver; a backend (the
 * platform audio_api, or a test stub) performs it.
 *
 * The ripped audio data (SPC program, BRR samples with per-sample
 * frequency + loop start, per-song sample banks and scripts) is exposed
 * so backends can speak real SPC when they grow into it.
 */
#ifndef FF4_AUDIO_ENGINE_H
#define FF4_AUDIO_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ff4/core/ff4_types.h"
#include "ff4/engine/asset_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== */
/*  Backend                                                             */
/* ==================================================================== */

/**
 * Backend callbacks a platform implements (defaults bind to the
 * platform audio_api). All volume values are 0..31 like $2100 style.
 */
typedef struct FF4_AudioBackend {
    void (*init)(void);
    void (*play_song)(void *opaque, int track);
    void (*stop_song)(void *opaque);
    void (*set_volume)(void *opaque, int volume);
    void (*play_sfx)(void *opaque, int sfx);
    void (*reset)(void *opaque);       /* spc reset / SDL reopen */
} FF4_AudioBackend;

/* ==================================================================== */
/*  Driver                                                              */
/* ==================================================================== */

typedef enum FF4_AudioState {
    FF4_AUDIO_IDLE = 0,
    FF4_AUDIO_FADING_OUT,    /**< Waiting out a fade before the song swaps. */
    FF4_AUDIO_FADING_IN      /**< New song playing, ramping volume. */
} FF4_AudioState;

typedef struct FF4_AudioDriver {
    /* the $1E00-$1E08 mail-box shadow */
    uint8_t cmd;             /**< $1E00: pending sound command. */
    uint8_t p1, p2, p3;      /**< $1E01-$1E03: command parameters. */
    uint8_t current_song;    /**< $1E05: song id. */
    uint8_t spc_sync;        /**< $1E08: fade sync counter. */

    uint8_t volume;          /**< Current driver volume (0..31). */

    FF4_AudioState state;
    int pending_song;        /**< Song waiting after the fade-out. */
    uint32_t fade_timer;     /**< Frames left in the current fade. */
    uint32_t fade_speed;     /**< Fade speed (frames per volume step). */

    FF4_AudioBackend backend;
    void *backend_opaque;
} FF4_AudioDriver;

/** Bind a driver and reset it to the boot state (mailbox cleared). */
void ff4_audio_driver_init(FF4_AudioDriver *d, const FF4_AudioBackend *backend,
                    void *opaque);

/**
 * Execute a sound command (ExecSound port): writes the mailbox and
 * applies the semantics - song play (fade out, swap, fade in), SFX,
 * volume and reset. Battle/menu call this with FF4_SoundCommand codes.
 */
void ff4_audio_exec(FF4_AudioDriver *d, FF4_SoundCommand cmd,
                    uint8_t p1, uint8_t p2, uint8_t p3);

/** Convenience: play a BGM track (the fade-out/swap/fade-in dance). */
void ff4_audio_play_song(FF4_AudioDriver *d, uint8_t track);

/** Convenience: immediate SFX. */
void ff4_audio_play_sfx(FF4_AudioDriver *d, uint8_t sfx);

/**
 * Advance the driver (fades, pending song handover). Call once per
 * frame from the host loop.
 */
void ff4_audio_tick(FF4_AudioDriver *d);

/* ==================================================================== */
/*  Ripped audio data accessors                                         */
/* ==================================================================== */

/** SPC-700 program blob (spcData) and its length. */
const uint8_t *ff4_audio_spc_program(const FF4_AssetDB *db, size_t *len);

/** Per-song SPC script (songScripts[id]). */
const uint8_t *ff4_audio_song_script(const FF4_AssetDB *db, uint8_t id,
                                     size_t *len);

/** Per-song sample bank (songSamples[id], 32-byte SPC bank list). */
const uint8_t *ff4_audio_song_samples(const FF4_AssetDB *db, uint8_t id,
                                      size_t *len);

/** BRR sample raw block + loop start + frequency multiplier. */
const uint8_t *ff4_audio_sample(const FF4_AssetDB *db, uint8_t idx,
                                size_t *len, uint16_t *loop, uint16_t *freq);

#define FF4_AUDIO_SONG_COUNT 70
#define FF4_AUDIO_SAMPLE_COUNT 23

#ifdef __cplusplus
}
#endif

#endif /* FF4_AUDIO_ENGINE_H */