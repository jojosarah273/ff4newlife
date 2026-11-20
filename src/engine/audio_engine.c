/**
 * @file audio_engine.c
 * @brief Phase 8: audio driver abstraction (SPC mailbox model).
 *
 * Mirrors sound/sound.asm (InitSound/ExecSound) at the logic level:
 * the driver owns the $1E00-$1E08 mailbox, fades and the song handover;
 * the backend owns the device.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/audio_engine.h"

/* ==================================================================== */
/*  Default backend: silent no-ops. Platforms inject their real backend */
/*  (e.g. the SDL adapter) via ff4_audio_driver_init().                        */
/* ==================================================================== */

static void be_nop1(void)                      { }
static void be_nop2(void *o)                   { (void)o; }
static void be_nop3(void *o, int v)            { (void)o; (void)v; }

/* ==================================================================== */
/*  Driver                                                              */
/* ==================================================================== */

void ff4_audio_driver_init(FF4_AudioDriver *d, const FF4_AudioBackend *backend,
                    void *opaque)
{
    memset(d, 0, sizeof *d);
    if (backend)
        d->backend = *backend;
    else {
        d->backend.init = be_nop1;
        d->backend.play_song = be_nop3;
        d->backend.stop_song = be_nop2;
        d->backend.set_volume = be_nop3;
        d->backend.play_sfx = be_nop3;
        d->backend.reset = be_nop2;
    }
    d->backend_opaque = opaque;
    d->state = FF4_AUDIO_IDLE;
    d->volume = 31;                      /* full volume */
    d->spc_sync = 0;
    if (d->backend.init)
        d->backend.init();
}

static int fade_frames_for_speed(int speed)
{
    /* The game's fade registers step a volume counter; the exact frame
       pacing lives in the SPC. Use a deterministic mapping here that
       backends may override: 16 frames per volume step, scaled down by
       the fast flag via the command byte. */
    return (speed > 0 ? speed : 1) * 16;
}

void ff4_audio_play_song(FF4_AudioDriver *d, uint8_t track)
{
    d->cmd = FF4_SND_PLAY_SONG;
    d->p1 = track;
    d->current_song = track;
    if (d->backend.play_song)
        d->backend.play_song(d->backend_opaque, track);
    d->state = FF4_AUDIO_IDLE;
}

void ff4_audio_play_sfx(FF4_AudioDriver *d, uint8_t sfx)
{
    d->cmd = FF4_SND_PLAY_SFX;
    d->p1 = sfx;
    if (d->backend.play_sfx)
        d->backend.play_sfx(d->backend_opaque, sfx);
}

void ff4_audio_exec(FF4_AudioDriver *d, FF4_SoundCommand cmd,
                    uint8_t p1, uint8_t p2, uint8_t p3)
{
    d->cmd = (uint8_t)cmd;
    d->p1 = p1;
    d->p2 = p2;
    d->p3 = p3;

    switch (cmd) {
    case FF4_SND_PLAY_SONG:
        ff4_audio_play_song(d, p1);
        break;
    case FF4_SND_PLAY_SFX:
        ff4_audio_play_sfx(d, p1);
        break;

    /* fades */
    case FF4_SND_FADE_OUT:
    case FF4_SND_FADE_OUT_SLOW:
    case FF4_SND_FADE_OUT_FAST:
        d->state = FF4_AUDIO_FADING_OUT;
        d->fade_timer = fade_frames_for_speed(
                            cmd == FF4_SND_FADE_OUT_FAST ? 4 : 8);
        if (cmd == FF4_SND_FADE_OUT_FAST && d->backend.stop_song)
            d->backend.stop_song(d->backend_opaque);
        break;
    case FF4_SND_FADE_HALF:
        d->volume = 15;
        if (d->backend.set_volume)
            d->backend.set_volume(d->backend_opaque, d->volume);
        break;

    /* volume */
    case FF4_SND_VOLUME_FULL:
        d->volume = 31;
        if (d->backend.set_volume)
            d->backend.set_volume(d->backend_opaque, d->volume);
        d->state = FF4_AUDIO_IDLE;
        break;
    case FF4_SND_VOLUME_QUARTER:
        d->volume = 8;
        if (d->backend.set_volume)
            d->backend.set_volume(d->backend_opaque, d->volume);
        break;

    case FF4_SND_RESET:
        d->state = FF4_AUDIO_IDLE;
        d->volume = 31;
        d->spc_sync = 0;
        if (d->backend.reset)
            d->backend.reset(d->backend_opaque);
        break;

    default:
        break;
    }
}

void ff4_audio_tick(FF4_AudioDriver *d)
{
    if (d->state == FF4_AUDIO_FADING_OUT) {
        if (d->fade_timer) {
            d->fade_timer--;
            if (d->volume > 0 && (d->fade_timer % 4) == 0)
                d->volume--;
        }
        if (d->fade_timer == 0) {
            /* swap in the pending song and ramp back up */
            if (d->pending_song >= 0 &&
                d->backend.play_song) {
                d->current_song = (uint8_t)d->pending_song;
                d->backend.play_song(d->backend_opaque, d->pending_song);
            }
            d->pending_song = -1;
            d->state = FF4_AUDIO_FADING_IN;
            d->fade_timer = fade_frames_for_speed(4);
        }
    } else if (d->state == FF4_AUDIO_FADING_IN) {
        if (d->fade_timer) {
            d->fade_timer--;
            if (d->volume < 31 && (d->fade_timer % 4) == 0)
                d->volume++;
        }
        if (d->fade_timer == 0) {
            d->volume = 31;
            if (d->backend.set_volume)
                d->backend.set_volume(d->backend_opaque, d->volume);
            d->state = FF4_AUDIO_IDLE;
        }
    }
}

/* ==================================================================== */
/*  Ripped data accessors                                               */
/* ==================================================================== */

const uint8_t *ff4_audio_spc_program(const FF4_AssetDB *db, size_t *len)
{
    const FF4_Blob *b = ff4_db_blob(db, "spcData");

    if (len)
        *len = b ? b->len : 0;
    return b ? b->data : NULL;
}

const uint8_t *ff4_audio_song_script(const FF4_AssetDB *db, uint8_t id,
                                     size_t *len)
{
    char key[40];
    const FF4_Blob *b;

    snprintf(key, sizeof key, "songScripts[%d]", id);
    b = ff4_db_blob(db, key);
    if (len)
        *len = b ? b->len : 0;
    return b ? b->data : NULL;
}

const uint8_t *ff4_audio_song_samples(const FF4_AssetDB *db, uint8_t id,
                                      size_t *len)
{
    char key[40];
    const FF4_Blob *b;

    snprintf(key, sizeof key, "songSamples[%d]", id);
    b = ff4_db_blob(db, key);
    if (len)
        *len = b ? b->len : 0;
    return b ? b->data : NULL;
}

const uint8_t *ff4_audio_sample(const FF4_AssetDB *db, uint8_t idx,
                                size_t *len, uint16_t *loop, uint16_t *freq)
{
    char key[40];
    const FF4_Blob *b;

    snprintf(key, sizeof key, "brrSamples[%d]", idx);
    b = ff4_db_blob(db, key);
    if (len)
        *len = b ? b->len : 0;
    if (loop) *loop = 0;
    if (freq) *freq = 0;

    /* loop start and frequency multiplier are 1-3 byte values */
    snprintf(key, sizeof key, "brrSampleLoopStart[%d]", idx);
    {
        const FF4_Blob *l = ff4_db_blob(db, key);

        if (l && l->len >= 2 && loop)
            *loop = (uint16_t)(l->data[0] | (l->data[1] << 8));
    }
    snprintf(key, sizeof key, "brrSampleFrequencyMultiplier[%d]", idx);
    {
        const FF4_Blob *f = ff4_db_blob(db, key);

        if (f && f->len >= 1 && freq)
            *freq = f->data[0];
    }
    return b ? b->data : NULL;
}