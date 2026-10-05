/*
 * platform/src/host_main.c
 *
 * Program entry point. Replaces the GBA's crt0.s + AgbMain (refrence/src/
 * main.c, excluded from the host build).
 *
 * Boots the platform layer (window), then the game (AgbMain), and runs the
 * game's frames until the window is closed or the frame limit is reached.
 * Each frame: the game's logic and interrupts, then the display.
 */

#include "platform/host_game.h"
#include "platform/host_render.h"
#include "platform/main_loop.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SCALE 3
#define MAX_SCALE 16

static void Usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [-d DATA_DIR] [-s SAVE_DIR] [-x SCALE] [-f FRAMES] [-o FILE]\n"
            "  -d DATA_DIR  converted game data (default: assets)\n"
            "  -s SAVE_DIR  save files (default: saves)\n"
            "  -x SCALE     initial window size as a multiple of 240x160 (default: %d)\n"
            "  -f FRAMES    stop after FRAMES frames (default: run until the window is closed)\n"
            "  -o FILE      on exit, save the last frame to FILE (binary PPM)\n",
            argv0, DEFAULT_SCALE);
}

static bool ParseNumber(const char *value, unsigned long max, unsigned long *out)
{
    char *end;

    *out = strtoul(value, &end, 10);
    return *value != '\0' && *end == '\0' && *out <= max;
}

/* Binary PPM: the simplest image format any viewer or converter reads. */
static bool SaveFrame(const char *path, const uint32_t *framebuffer)
{
    FILE *f = fopen(path, "wb");
    int i;

    if (f == NULL)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT);
    for (i = 0; i < PLATFORM_SCREEN_WIDTH * PLATFORM_SCREEN_HEIGHT; i++)
    {
        unsigned char rgb[3] = { framebuffer[i] >> 16, framebuffer[i] >> 8, framebuffer[i] };
        fwrite(rgb, 1, sizeof(rgb), f);
    }
    return fclose(f) == 0;
}

static void RunGameFrame(void)
{
    HostMain_RunFrame();
    Host_RenderFrame(Platform_GetFramebuffer());
}

int main(int argc, char **argv)
{
    struct PlatformConfig config = {
        .dataDir = "assets",
        .saveDir = "saves",
        .scale = DEFAULT_SCALE,
    };
    unsigned long maxFrames = 0;
    const char *framePath = NULL;
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
        if (arg[0] == '-' && strchr("dsxfo", arg[1]) != NULL && arg[1] != '\0' && arg[2] == '\0' && i + 1 < argc)
        {
            const char *value = argv[++i];
            unsigned long scale;

            switch (arg[1])
            {
            case 'd':
                config.dataDir = value;
                break;
            case 's':
                config.saveDir = value;
                break;
            case 'o':
                framePath = value;
                break;
            case 'x':
                if (!ParseNumber(value, MAX_SCALE, &scale) || scale == 0)
                {
                    fprintf(stderr, "%s: invalid scale '%s' (1-%d)\n", argv[0], value, MAX_SCALE);
                    return 2;
                }
                config.scale = (int)scale;
                break;
            case 'f':
                if (!ParseNumber(value, UINT32_MAX, &maxFrames))
                {
                    fprintf(stderr, "%s: invalid frame count '%s'\n", argv[0], value);
                    return 2;
                }
                break;
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
    Host_SetFrameCallback(RunGameFrame);

    startNs = Platform_GetTimeNs();
    framesRun = Host_RunMainLoop((uint32_t)maxFrames);
    printf("ran %u frames in %.3f s\n", framesRun, (double)(Platform_GetTimeNs() - startNs) / 1e9);

    if (framePath != NULL && !SaveFrame(framePath, Platform_GetFramebuffer()))
        fprintf(stderr, "%s: could not write '%s'\n", argv[0], framePath);

    Platform_Shutdown();
    return 0;
}
