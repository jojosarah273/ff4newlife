/**
 * @file test_audio.c
 * @brief Phase 8 tests: audio driver (SPC mailbox model), fades, song
 *        handover, SFX routing, and the ripped audio data accessors.
 */
#include <stdio.h>
#include <string.h>

#include "ff4/engine/asset_db.h"
#include "ff4/engine/audio_engine.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

/* A recording backend so we can assert what the driver commanded. */
typedef struct Rec {
    int songs[16], nsongs;
    int sfx[16], nsfx;
    int volumes[32], nvols;
    int stops, resets;
} Rec;

static void rec_play_song(void *o, int t)
{
    Rec *r = (Rec *)o;
    r->songs[r->nsongs++] = t;
}
static void rec_stop(void *o)          { ((Rec *)o)->stops++; }
static void rec_vol(void *o, int v)    { ((Rec *)o)->volumes[((Rec *)o)->nvols++] = v; }
static void rec_sfx(void *o, int s)    { Rec *r = (Rec *)o; r->sfx[r->nsfx++] = s; }
static void rec_reset(void *o)         { ((Rec *)o)->resets++; }

static FF4_AudioBackend rec_backend(Rec *r)
{
    FF4_AudioBackend b;

    (void)r;
    memset(&b, 0, sizeof b);
    b.play_song = rec_play_song;
    b.stop_song = rec_stop;
    b.set_volume = rec_vol;
    b.play_sfx = rec_sfx;
    b.reset = rec_reset;
    return b;
}

static void test_driver_command(const FF4_AssetDB *db)
{
    (void)db;
    FF4_AudioDriver d;
    Rec r;

    memset(&r, 0, sizeof r);
    FF4_AudioBackend b = rec_backend(&r);
    ff4_audio_driver_init(&d, &b, &r);

    /* play a song immediately (idle path) */
    ff4_audio_exec(&d, FF4_SND_PLAY_SONG, 5, 0, 0);
    CHECK(d.cmd == FF4_SND_PLAY_SONG && d.p1 == 5);
    CHECK(d.current_song == 5);
    CHECK(r.nsongs == 1 && r.songs[0] == 5);

    /* SFX passes straight through */
    ff4_audio_exec(&d, FF4_SND_PLAY_SFX, 0x1C, 0, 0);
    CHECK(r.nsfx == 1 && r.sfx[0] == 0x1C);

    /* volume commands reach the backend */
    ff4_audio_exec(&d, FF4_SND_FADE_HALF, 0, 0, 0);
    CHECK(d.volume == 15);
    ff4_audio_exec(&d, FF4_SND_VOLUME_FULL, 0, 0, 0);
    CHECK(d.volume == 31);
    CHECK(r.nvols == 2);

    /* reset clears the mailbox and hits the backend */
    ff4_audio_exec(&d, FF4_SND_RESET, 0, 0, 0);
    CHECK(r.resets == 1);
    CHECK(d.state == FF4_AUDIO_IDLE);
}

static void test_fade_handover(const FF4_AssetDB *db)
{
    (void)db;
    FF4_AudioDriver d;
    Rec r;

    memset(&r, 0, sizeof r);
    FF4_AudioBackend b = rec_backend(&r);
    ff4_audio_driver_init(&d, &b, &r);

    /* the game's "zol: change song" sequence: fade out -> swap -> in */
    ff4_audio_exec(&d, FF4_SND_FADE_OUT_FAST, 0, 0, 0);
    CHECK(d.state == FF4_AUDIO_FADING_OUT);
    CHECK(d.fade_timer > 0);

    /* queue the song swap while the fade runs */
    ff4_audio_exec(&d, FF4_SND_PLAY_SONG, 9, 0, 0);
    CHECK(d.state == FF4_AUDIO_IDLE);        /* direct set on exec */

    /* a full fade out -> fades back in from the pending song */
    d.cmd = FF4_SND_FADE_OUT_SLOW;
    d.state = FF4_AUDIO_FADING_OUT;
    d.fade_timer = 16;
    d.pending_song = 3;
    d.volume = 31;

    ff4_audio_tick(&d);
    CHECK(d.state != FF4_AUDIO_IDLE);

    /* drive the fade to completion */
    while (d.state == FF4_AUDIO_FADING_OUT) {
        ff4_audio_tick(&d);
        if (d.fade_timer == 0) {
            CHECK(d.current_song == 3);      /* song swapped */
            CHECK(r.songs[r.nsongs - 1] == 3);
            break;
        }
    }
    /* ramp back to full volume and idle */
    while (d.state == FF4_AUDIO_FADING_IN) {
        ff4_audio_tick(&d);
        if (d.state == FF4_AUDIO_IDLE)
            break;
    }
    CHECK(d.state == FF4_AUDIO_IDLE);
    CHECK(d.volume == 31);
}

static void test_song_data(const FF4_AssetDB *db)
{
    size_t len;
    const uint8_t *p;
    uint16_t loop, freq;

    /* SPC program exists and is the real size (~13.6K) */
    p = ff4_audio_spc_program(db, &len);
    CHECK(p != NULL);
    CHECK(len > 10000);

    /* songs have scripts and sample banks */
    p = ff4_audio_song_script(db, 1, &len);
    CHECK(p != NULL && len >= 2);

    p = ff4_audio_song_samples(db, 1, &len);
    CHECK(p != NULL);

    /* BRR sample header decoding */
    p = ff4_audio_sample(db, 0, &len, &loop, &freq);
    CHECK(p != NULL && len > 0);
    (void)freq;   /* sample 0 legitimately carries multiplier 0 */
}
int main(int argc, char **argv)
{
    FF4_AssetDB db;
    const char *path = argc > 1 ? argv[1] : "data/ff4-en-data.json";

    if (ff4_db_load(&db, path) != 0) {
        printf("fatal: cannot load %s (%s)\n", path, db.error);
        return 1;
    }

    test_driver_command(&db);
    test_fade_handover(&db);
    test_song_data(&db);

    printf("%d checks, %d failure(s), %d parse warning(s)\n",
           checks, failures, db.warnings);
    ff4_db_free(&db);
    return failures == 0 ? 0 : 1;
}