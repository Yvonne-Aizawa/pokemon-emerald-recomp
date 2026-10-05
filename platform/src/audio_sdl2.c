/*
 * platform/src/audio_sdl2.c
 *
 * Output device for host_audio.c: an SDL audio device whose callback drains
 * the ring buffer. SDL converts to the device's own format and rate if they
 * differ.
 */

#include "platform/host_audio.h"

#include <SDL.h>
#include <stdio.h>

#define DEVICE_BUFFER_FRAMES 1024  /* ~21 ms per callback */

static SDL_AudioDeviceID sDevice;

static void SDLCALL FillAudio(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    HostAudio_Read((int16_t *)stream, len / (int)(2 * sizeof(int16_t)));
}

bool HostAudio_OpenDevice(void)
{
    SDL_AudioSpec want, have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "audio: %s (continuing without sound)\n", SDL_GetError());
        return false;
    }

    SDL_zero(want);
    want.freq = HOST_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = DEVICE_BUFFER_FRAMES;
    want.callback = FillAudio;
    sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sDevice == 0)
    {
        fprintf(stderr, "audio: %s (continuing without sound)\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    printf("audio: %s, %d Hz\n", SDL_GetCurrentAudioDriver(), have.freq);
    HostAudio_SetEnabled(true);
    SDL_PauseAudioDevice(sDevice, 0);
    return true;
}

void HostAudio_CloseDevice(void)
{
    uint32_t underruns, dropped;

    if (sDevice == 0)
        return;
    HostAudio_SetEnabled(false);
    HostAudio_GetStats(&underruns, &dropped);
    printf("audio: %.2f s of underruns, %.2f s dropped\n",
           (double)underruns / HOST_AUDIO_RATE, (double)dropped / HOST_AUDIO_RATE);
    SDL_CloseAudioDevice(sDevice);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sDevice = 0;
}
