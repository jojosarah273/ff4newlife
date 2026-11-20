/**
 * @file audio_api.h
 * @brief Abstract audio queue. Core logic enqueues songs/SFX by id (the
 *        $1E00 mail-box model in spirit); the backend owns the device.
 *        Phase 8 converts the SPC700 command stream into this API.
 */
#ifndef FF4_AUDIO_API_H
#define FF4_AUDIO_API_H

#ifdef __cplusplus
extern "C" {
#endif

int  ff4_audio_init(void);
void ff4_audio_queue_song(int track);
void ff4_audio_queue_sfx(int sfx);
void ff4_audio_fade_out(int fast);
void ff4_audio_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* FF4_AUDIO_API_H */