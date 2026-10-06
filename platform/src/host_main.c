/*
 * platform/src/host_main.c
 *
 * Program entry point. Replaces the GBA's crt0.s + AgbMain (reference/src/
 * main.c, excluded from the host build).
 *
 * Boots the platform layer (window), then the game (AgbMain), and runs the
 * game's frames until the window is closed or the frame limit is reached.
 * Each frame: the keypad is read, then the game's logic and interrupts run,
 * then the display is drawn.
 */

#include "platform/crash_handler.h"
#include "platform/host_audio.h"
#include "platform/host_game.h"
#include "platform/host_input.h"
#include "platform/host_render.h"
#include "platform/host_save.h"
#include "platform/irq_timer.h"
#include "platform/main_loop.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SCALE 3
#define MAX_SCALE 16

/* Scripted input (-i): long enough for the game to see a press and release. */
#define DEFAULT_HOLD_FRAMES 5
#define MAX_SCRIPT_STEPS 256

struct ScriptStep
{
    uint32_t frame;
    uint32_t hold;
    uint16_t buttons;
};

static struct ScriptStep sScript[MAX_SCRIPT_STEPS];
static int sScriptLength;

/* -a: the game's sound, written to a WAV file as it is rendered. */
static FILE *sAudioDump;
static uint32_t sAudioDumpFrames;

static const struct { const char *name; uint16_t button; } sButtonNames[] = {
    { "a", PLATFORM_BUTTON_A },         { "b", PLATFORM_BUTTON_B },
    { "select", PLATFORM_BUTTON_SELECT }, { "start", PLATFORM_BUTTON_START },
    { "up", PLATFORM_BUTTON_UP },       { "down", PLATFORM_BUTTON_DOWN },
    { "left", PLATFORM_BUTTON_LEFT },   { "right", PLATFORM_BUTTON_RIGHT },
    { "l", PLATFORM_BUTTON_L },         { "r", PLATFORM_BUTTON_R },
};

static void Usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [-d DATA_DIR] [-s SAVE_DIR] [-x SCALE] [-f FRAMES] [-o FILE] [-a FILE] [-i SCRIPT] [--fast] [--mute] [--smooth-sound]\n"
            "  -d DATA_DIR  converted game data (default: assets)\n"
            "  -s SAVE_DIR  save files and crash reports (default: ./saves if it holds\n"
            "               a save, otherwise the per-user data directory)\n"
            "  -x SCALE     initial window size as a multiple of 240x160 (default: %d)\n"
            "  -f FRAMES    stop after FRAMES frames (default: run until the window is closed)\n"
            "  -o FILE      on exit, save the last frame to FILE (binary PPM)\n"
            "  -a FILE      write the sound to FILE (WAV, 48 kHz stereo); works with --fast\n"
            "  -i SCRIPT    press buttons at given frames, for testing: comma-separated\n"
            "               FRAME[+HOLD]:BUTTON[|BUTTON...], e.g. 600:a,3700+10:select\n"
            "               (buttons: a b select start up down left right l r; HOLD\n"
            "               defaults to %d frames)\n"
            "  --fast       don't wait between frames (scripted test runs); implies --mute\n"
            "  --mute       no sound\n"
            "  --smooth-sound  interpolated, low-passed sound instead of the exact GBA output\n",
            argv0, DEFAULT_SCALE, DEFAULT_HOLD_FRAMES);
}

/* Where saves went before the game had a per-user directory; still used when
 * it holds a save, so existing saves keep working. */
#define LEGACY_SAVE_DIR "saves"

static const char *DefaultSaveDir(void)
{
    FILE *legacy = fopen(LEGACY_SAVE_DIR "/" HOST_SAVE_FILE_NAME, "rb");
    const char *userDir;

    if (legacy != NULL)
    {
        fclose(legacy);
        return LEGACY_SAVE_DIR;
    }
    userDir = Platform_GetUserDataDir();
    return userDir != NULL ? userDir : LEGACY_SAVE_DIR;
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

/* Parses "FRAME[+HOLD]:BUTTON[|BUTTON...],..." into sScript. */
static bool ParseScript(const char *text)
{
    char buffer[4096];
    char *step, *saveStep;

    if (strlen(text) >= sizeof(buffer))
        return false;
    strcpy(buffer, text);
    for (step = strtok_r(buffer, ",", &saveStep); step != NULL; step = strtok_r(NULL, ",", &saveStep))
    {
        struct ScriptStep *out;
        char *colon = strchr(step, ':');
        char *plus, *name, *saveName, *end;

        if (colon == NULL || sScriptLength == MAX_SCRIPT_STEPS)
            return false;
        *colon = '\0';
        out = &sScript[sScriptLength++];
        out->hold = DEFAULT_HOLD_FRAMES;
        if ((plus = strchr(step, '+')) != NULL)
        {
            *plus = '\0';
            out->hold = strtoul(plus + 1, &end, 10);
            if (plus[1] == '\0' || *end != '\0')
                return false;
        }
        out->frame = strtoul(step, &end, 10);
        if (*step == '\0' || *end != '\0')
            return false;
        out->buttons = 0;
        for (name = strtok_r(colon + 1, "|", &saveName); name != NULL; name = strtok_r(NULL, "|", &saveName))
        {
            size_t i;

            for (i = 0; i < sizeof(sButtonNames) / sizeof(sButtonNames[0]); i++)
            {
                if (strcmp(name, sButtonNames[i].name) == 0)
                    break;
            }
            if (i == sizeof(sButtonNames) / sizeof(sButtonNames[0]))
                return false;
            out->buttons |= sButtonNames[i].button;
        }
    }
    return true;
}

static uint16_t ScriptedButtons(uint32_t frame)
{
    uint16_t buttons = 0;
    int i;

    for (i = 0; i < sScriptLength; i++)
    {
        if (frame >= sScript[i].frame && frame - sScript[i].frame < sScript[i].hold)
            buttons |= sScript[i].buttons;
    }
    return buttons;
}

/* -a: a 48 kHz stereo 16-bit WAV file; the header is completed on close. */
static void WriteWavHeader(FILE *f, uint32_t frames)
{
    uint32_t dataBytes = frames * 4, rate = HOST_AUDIO_RATE, byteRate = HOST_AUDIO_RATE * 4;
    uint32_t riffBytes = 36 + dataBytes, fmtBytes = 16;
    uint16_t format = 1, channels = 2, blockAlign = 4, bits = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffBytes, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtBytes, 4, 1, f);
    fwrite(&format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
}

/* Write out rendered sound, keeping `keep` frames buffered so the renderer's
 * rate control (which aims at a buffer level) stays neutral. */
static void DrainAudioDump(int keep)
{
    static int16_t buffer[4096 * 2];
    int n;

    while (sAudioDump != NULL && (n = HostAudio_Buffered() - keep) > 0)
    {
        if (n > 4096)
            n = 4096;
        HostAudio_Read(buffer, n);
        fwrite(buffer, 4, (size_t)n, sAudioDump);
        sAudioDumpFrames += n;
    }
}

/* How long a frame may run before interrupts arrive mid-frame: the GBA frame
 * time when paced; with --fast, only frames stuck in a busy-wait (see
 * platform/irq_timer.h). */
#define STALL_INTERVAL_NS 100000000ul
static unsigned long sIrqIntervalNs = HOST_FRAME_NS;

static void RunGameFrame(void)
{
    Host_SetKeypad(Platform_GetButtons() | ScriptedButtons(Host_GetFrameCount()));

    /* If the game's frame overruns (it busy-waits for something an
     * interrupt does, or is just slow), interrupts arrive mid-frame as on
     * hardware. */
    IrqTimer_Arm(HostMain_RaiseVBlankInterrupts, sIrqIntervalNs);
    HostMain_RunFrame();
    IrqTimer_Disarm();

    Host_RenderFrame(Platform_GetFramebuffer());

    /* Persist the save chip if the game wrote to it this frame. */
    Host_SaveFlush();

    DrainAudioDump(HOST_AUDIO_RATE / 20);
}

/* Game code waiting for V-blank inside a frame (the crash screen, debug
 * tools): end the frame there -- interrupts, display, input -- then wait out
 * the rest of the frame time, as the hardware would. */
static void WaitForVBlankInsideFrame(void)
{
    uint64_t start = Platform_GetTimeNs();
    uint64_t elapsed;

    IrqTimer_Disarm();
    HostMain_RaiseVBlankInterrupts();
    Host_RenderFrame(Platform_GetFramebuffer());
    Platform_FrameEnd();
    Platform_PollEvents();
    Host_SetKeypad(Platform_GetButtons());

    elapsed = Platform_GetTimeNs() - start;
    if (elapsed < HOST_FRAME_NS)
        Platform_SleepNs(HOST_FRAME_NS - elapsed);
    IrqTimer_Arm(HostMain_RaiseVBlankInterrupts, sIrqIntervalNs);
}

int main(int argc, char **argv)
{
    struct PlatformConfig config = {
        .dataDir = "assets",
        .scale = DEFAULT_SCALE,
    };
    unsigned long maxFrames = 0;
    bool sound = true;
    const char *framePath = NULL;
    const char *audioPath = NULL;
    uint64_t startNs;
    uint32_t framesRun;
    char crashReportPath[1024];
    int i;

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            Usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(arg, "--fast") == 0)
        {
            Host_SetPacing(false);
            sIrqIntervalNs = STALL_INTERVAL_NS;
            sound = false;
            continue;
        }
        if (strcmp(arg, "--mute") == 0)
        {
            sound = false;
            continue;
        }
        if (strcmp(arg, "--smooth-sound") == 0)
        {
            HostAudio_SetRawOutput(false);
            continue;
        }
        if (arg[0] == '-' && strchr("dsxfoia", arg[1]) != NULL && arg[1] != '\0' && arg[2] == '\0' && i + 1 < argc)
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
            case 'a':
                audioPath = value;
                break;
            case 'i':
                if (!ParseScript(value))
                {
                    fprintf(stderr, "%s: invalid input script '%s'\n", argv[0], value);
                    return 2;
                }
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

    if (config.saveDir == NULL)
        config.saveDir = DefaultSaveDir();
    snprintf(crashReportPath, sizeof(crashReportPath), "%s/%s", config.saveDir, CRASH_REPORT_FILE);
    Crash_Install(crashReportPath);
    Crash_SetFrameCounter(Host_GetFrameCount);
    Host_RegisterCrashWatches();

    if (Platform_Init(&config) != 0)
    {
        fprintf(stderr, "%s: platform init failed\n", argv[0]);
        return 1;
    }
    printf("boot ok\n");
    Host_SaveOpen(config.saveDir);
    fflush(stdout);

    AgbMain();
    printf("game init ok\n");
    if (audioPath != NULL)
    {
        sAudioDump = fopen(audioPath, "wb");
        if (sAudioDump == NULL)
        {
            fprintf(stderr, "%s: could not write '%s'\n", argv[0], audioPath);
            return 1;
        }
        WriteWavHeader(sAudioDump, 0);
        HostAudio_SetEnabled(true);
    }
    else if (sound)
    {
        HostAudio_OpenDevice();
    }
    fflush(stdout);
    Host_SetFrameCallback(RunGameFrame);
    gHostVBlankIntrWaitHandler = WaitForVBlankInsideFrame;

    startNs = Platform_GetTimeNs();
    framesRun = Host_RunMainLoop((uint32_t)maxFrames);
    printf("ran %u frames in %.3f s\n", framesRun, (double)(Platform_GetTimeNs() - startNs) / 1e9);
    Host_SaveFlush();

    if (framePath != NULL && !SaveFrame(framePath, Platform_GetFramebuffer()))
        fprintf(stderr, "%s: could not write '%s'\n", argv[0], framePath);

    if (sAudioDump != NULL)
    {
        DrainAudioDump(0);
        fseek(sAudioDump, 0, SEEK_SET);
        WriteWavHeader(sAudioDump, sAudioDumpFrames);
        fclose(sAudioDump);
    }
    HostAudio_CloseDevice();
    Platform_Shutdown();
    return 0;
}
