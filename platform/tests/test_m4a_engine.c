/*
 * platform/tests/test_m4a_engine.c
 *
 * The game's sound engine (m4a.c + m4a_engine.c) through the host sound
 * hardware (host_audio.c): music, a sound effect and Pokemon cries (DPCM-
 * compressed samples, forwards and reversed) must produce sound, and the
 * players' status must follow the song (playing, then faded out).
 */

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "m4a.h"
#include "sound.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "platform/host_audio.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

/* Run `frames` frames as the interrupts do (V-count, then V-blank) and
 * return the loudest output sample. */
static int RunFrames(int frames)
{
    static int16_t buffer[HOST_AUDIO_RATE / 10 * 2];
    int peak = 0;

    while (frames-- > 0)
    {
        int n, i;

        m4aSoundVSync();
        m4aSoundMain();
        n = HostAudio_Buffered();
        if (n > HOST_AUDIO_RATE / 10)
            n = HOST_AUDIO_RATE / 10;
        HostAudio_Read(buffer, n);
        for (i = 0; i < n * 2; i++)
        {
            if (abs(buffer[i]) > peak)
                peak = abs(buffer[i]);
        }
    }
    return peak;
}

int main(void)
{
    int peak, rest;

    m4aSoundInit();
    HostAudio_SetEnabled(true);
    Check(RunFrames(30) < 64, "silence before anything plays");

    m4aSongNumStart(MUS_LITTLEROOT);
    peak = RunFrames(300);
    printf("      music peak %d\n", peak);
    Check(peak > 1000, "music produces sound");
    Check((gMPlayInfo_BGM.status & MUSICPLAYER_STATUS_TRACK) != 0
       && !(gMPlayInfo_BGM.status & MUSICPLAYER_STATUS_PAUSE), "music is still playing after 5 s");
    Check(gMPlayInfo_BGM.songHeader == gSongTable[MUS_LITTLEROOT].header, "the BGM player has the song");

    m4aMPlayFadeOut(&gMPlayInfo_BGM, 2);
    RunFrames(40);
    Check((gMPlayInfo_BGM.status & MUSICPLAYER_STATUS_PAUSE) != 0, "fade-out stops the music in 16 steps");
    Check(RunFrames(30) < 1000, "quiet after the fade-out");

    m4aSongNumStart(SE_SELECT);
    peak = RunFrames(20);
    printf("      sound effect peak %d\n", peak);
    Check(peak > 1000, "sound effect produces sound");

    PlayCryInternal(SPECIES_TORCHIC, 0, 120, 10, CRY_MODE_NORMAL);
    peak = RunFrames(1);  /* the cry gets its channel when the sequencer runs */
    Check(IsCryPlaying(), "cry starts");
    rest = RunFrames(59);
    if (rest > peak)
        peak = rest;
    printf("      cry peak %d\n", peak);
    Check(peak > 1000, "compressed cry produces sound");
    RunFrames(120);
    Check(!IsCryPlaying(), "cry ends");

    PlayCryInternal(SPECIES_TORCHIC, 0, 120, 10, CRY_MODE_GROWL_1);
    peak = RunFrames(60);
    printf("      reversed cry peak %d\n", peak);
    Check(peak > 1000, "reversed compressed cry produces sound");

    printf(sFailures ? "m4a_engine: %d FAILED\n" : "m4a_engine: all tests passed\n", sFailures);
    return sFailures != 0;
}
