/*
 * platform/src/host_main.c
 *
 * Program entry point. Replaces the GBA's crt0.s + AgbMain (refrence/src/
 * main.c, excluded from the host build).
 *
 * Boots the platform layer, then the game (AgbMain), and runs the game's
 * frames until the frame limit or a quit request. Still headless: nothing
 * is drawn, played or read from input until Phases 5-13.
 */

#include "platform/host_game.h"
#include "platform/main_loop.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Phase 4 has no window to close, so by default stop after one second's
 * worth of frames. Phase 5 makes 0 (run until quit) the default. */
#define DEFAULT_FRAMES 60

static void Usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [-d DATA_DIR] [-s SAVE_DIR] [-f FRAMES]\n"
            "  -d DATA_DIR  converted game data (default: assets)\n"
            "  -s SAVE_DIR  save files (default: saves)\n"
            "  -f FRAMES    stop after FRAMES frames, 0 = run until quit (default: %d)\n",
            argv0, DEFAULT_FRAMES);
}

int main(int argc, char **argv)
{
    struct PlatformConfig config = {
        .dataDir = "assets",
        .saveDir = "saves",
    };
    unsigned long maxFrames = DEFAULT_FRAMES;
    uint64_t startNs;
    uint32_t framesRun;
    int i;

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            Usage(stdout, argv[0]);
            return 0;
        }
        if ((strcmp(arg, "-d") == 0 || strcmp(arg, "-s") == 0 || strcmp(arg, "-f") == 0) && i + 1 < argc)
        {
            const char *value = argv[++i];
            char *end;

            if (arg[1] == 'd')
                config.dataDir = value;
            else if (arg[1] == 's')
                config.saveDir = value;
            else
            {
                maxFrames = strtoul(value, &end, 10);
                if (*value == '\0' || *end != '\0' || maxFrames > UINT32_MAX)
                {
                    fprintf(stderr, "%s: invalid frame count '%s'\n", argv[0], value);
                    return 2;
                }
            }
            continue;
        }
        fprintf(stderr, "%s: unknown or incomplete argument '%s'\n", argv[0], arg);
        Usage(stderr, argv[0]);
        return 2;
    }

    if (Platform_Init(&config) != 0)
    {
        fprintf(stderr, "%s: platform init failed\n", argv[0]);
        return 1;
    }
    printf("boot ok\n");
    fflush(stdout);

    AgbMain();
    printf("game init ok\n");
    fflush(stdout);
    Host_SetFrameCallback(HostMain_RunFrame);

    startNs = Platform_GetTimeNs();
    framesRun = Host_RunMainLoop((uint32_t)maxFrames);
    printf("ran %u frames in %.3f s\n", framesRun, (double)(Platform_GetTimeNs() - startNs) / 1e9);

    Platform_Shutdown();
    return 0;
}
