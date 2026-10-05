/*
 * platform/include/platform/host_audio.h
 *
 * The GBA's sound hardware, for the game's own sound engine (m4a.c and
 * m4a_engine.c).
 *
 * Once per frame, after mixing, SoundMain hands over that frame's Direct
 * Sound samples (FIFO A = right, FIFO B = left, at the engine's sample rate).
 * The host renders the frame: those samples plus the four PSG channels
 * (square 1/2, wave, noise) as the game left the sound registers, mixed the
 * way the hardware mixes them (SOUNDCNT_L/H/X, SOUNDBIAS). The result goes to
 * a ring buffer that the output device drains.
 *
 * As on hardware, Direct Sound plays one frame after it is mixed (the DMA
 * plays the previous segment while SoundMain fills the next), while PSG
 * register writes take effect at once.
 */

#ifndef PLATFORM_HOST_AUDIO_H
#define PLATFORM_HOST_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_AUDIO_RATE 48000

/* From SoundMain, inside its lock; may run in a signal handler. Does
 * nothing until HostAudio_SetEnabled(true). */
void HostAudio_SoundFrame(const int8_t *right, const int8_t *left, int count, int pcmFreq);

/* Render frames into the ring buffer (off by default: e.g. --fast runs
 * produce frames far faster than real time). */
void HostAudio_SetEnabled(bool enabled);

/* Exact DAC output (held samples, 8-bit steps; the default) or smoothed
 * output (false); see host_audio.c. */
void HostAudio_SetRawOutput(bool raw);

/* Glitch counters, in output frames: the device ran dry (crackle), or
 * rendered sound didn't fit the buffer (skips). */
void HostAudio_GetStats(uint32_t *underrunFrames, uint32_t *droppedFrames);

/* Rendered stereo frames waiting in the ring buffer. */
int HostAudio_Buffered(void);

/* Take `frames` stereo frames (interleaved left/right); missing frames are
 * silence. Called from the output device's thread. Returns the number of
 * frames that were available. */
int HostAudio_Read(int16_t *out, int frames);

/* Output device (audio_sdl2.c). Open starts playback and enables rendering;
 * failure is not fatal (the game runs silently). */
bool HostAudio_OpenDevice(void);
void HostAudio_CloseDevice(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_AUDIO_H */
