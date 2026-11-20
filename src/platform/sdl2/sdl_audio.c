/**
 * @file sdl_audio.c
 * @brief SDL2 adapter for the audio API. Queues sound commands from core
 *        (Phase 8 will convert SPC-format tracks), for now fades out and
 *        opens a silent device so the pipeline is exercised end to end.
 */
#include <SDL.h>

#include "ff4/platform/audio_api.h"

static SDL_AudioDeviceID device;

int ff4_audio_init(void)
{
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = 32000;
    spec.format = AUDIO_S16SYS;
    spec.channels = 1;
    spec.samples = 512;
    device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    if (device != 0)
        SDL_PauseAudioDevice(device, 0);
    return device == 0 ? -1 : 0;
}

void ff4_audio_queue_song(int track)      { (void)track; }
void ff4_audio_queue_sfx(int sfx)         { (void)sfx; }
void ff4_audio_fade_out(int fast)         { (void)fast; }

void ff4_audio_quit(void)
{
    if (device != 0)
        SDL_CloseAudioDevice(device);
}