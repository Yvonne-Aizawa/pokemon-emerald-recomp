/*
 * platform/src/platform_sdl2.c
 *
 * SDL2 implementation of platform.h: a window showing the 240x160
 * framebuffer at an integer scale, the OS event loop, input (via
 * input_sdl2.c), and timing. F11 or Alt+Enter switch between the window and
 * borderless fullscreen (the desktop's resolution, letterboxed); Tab switches
 * fast-forward (Platform_TakeFastForwardToggle).
 *
 * Runs without a display under SDL_VIDEODRIVER=dummy (the tests do this).
 */

#include "platform/platform.h"
#include "input_sdl2.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_TITLE "pkmemerald"

static struct PlatformConfig sConfig;
static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sScreen;
static uint32_t sFramebuffer[PLATFORM_SCREEN_WIDTH * PLATFORM_SCREEN_HEIGHT];
static uint64_t sStartCounter;
static uint64_t sCounterFrequency;
static bool sQuitRequested;
static unsigned sFastForwardToggles;  /* Tab presses not yet taken */

static const char *RendererName(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;

    return SDL_GetRendererInfo(renderer, &info) == 0 ? info.name : "unknown";
}

int Platform_Init(const struct PlatformConfig *config)
{
    int scale;

    sConfig = *config;
    scale = sConfig.scale > 0 ? sConfig.scale : 3;
    sQuitRequested = false;

    /* SDL's default Ctrl-C handling turns SIGINT/SIGTERM into SDL_QUIT. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    /* The window size also applies when leaving fullscreen. */
    sWindow = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               PLATFORM_SCREEN_WIDTH * scale, PLATFORM_SCREEN_HEIGHT * scale,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
                               | (sConfig.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (sWindow == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    /* No vsync: the main loop paces frames at the GBA's 59.73 Hz itself. */
    sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
    if (sRenderer == NULL)
        sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    if (sRenderer == NULL)
    {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(sWindow);
        SDL_Quit();
        return -1;
    }

    /* Keep pixels sharp: scale by whole multiples, letterbox the rest. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(sRenderer, PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT);
    SDL_RenderSetIntegerScale(sRenderer, SDL_TRUE);

    sScreen = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT);
    if (sScreen == NULL)
    {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        Platform_Shutdown();
        return -1;
    }

    Input_Init(sConfig.keys);

    /* The GBA powers on to a white screen. */
    memset(sFramebuffer, 0xFF, sizeof(sFramebuffer));

    printf("video: %s (%s renderer)\n", SDL_GetCurrentVideoDriver(), RendererName(sRenderer));

    sCounterFrequency = SDL_GetPerformanceFrequency();
    sStartCounter = SDL_GetPerformanceCounter();
    return 0;
}

void Platform_Shutdown(void)
{
    Input_Shutdown();
    if (sScreen != NULL)
        SDL_DestroyTexture(sScreen);
    if (sRenderer != NULL)
        SDL_DestroyRenderer(sRenderer);
    if (sWindow != NULL)
        SDL_DestroyWindow(sWindow);
    sScreen = NULL;
    sRenderer = NULL;
    sWindow = NULL;
    SDL_Quit();
}

const char *Platform_GetDataDir(void)
{
    return sConfig.dataDir;
}

const char *Platform_GetSaveDir(void)
{
    return sConfig.saveDir;
}

const char *Platform_GetUserDataDir(void)
{
    static char *sPath;

    if (sPath == NULL && (sPath = SDL_GetPrefPath("", "pkmemerald")) != NULL)
    {
        size_t length = strlen(sPath);

        if (length > 1 && (sPath[length - 1] == '/' || sPath[length - 1] == '\\'))
            sPath[length - 1] = '\0';
    }
    return sPath;
}

uint32_t *Platform_GetFramebuffer(void)
{
    return sFramebuffer;
}

void Platform_FrameBegin(void)
{
}

void Platform_FrameEnd(void)
{
    SDL_UpdateTexture(sScreen, NULL, sFramebuffer, PLATFORM_SCREEN_WIDTH * sizeof(sFramebuffer[0]));
    SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);  /* letterbox bars */
    SDL_RenderClear(sRenderer);
    SDL_RenderCopy(sRenderer, sScreen, NULL, NULL);
    SDL_RenderPresent(sRenderer);
}

static void ToggleFullscreen(void)
{
    bool fullscreen = (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

    if (SDL_SetWindowFullscreen(sWindow, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        fprintf(stderr, "video: could not switch fullscreen: %s\n", SDL_GetError());
}

/* F11 or Alt+Enter; these key presses don't reach the game. */
static bool IsFullscreenToggle(const SDL_Event *event)
{
    const SDL_Keysym *key = &event->key.keysym;

    if (event->type != SDL_KEYDOWN)
        return false;
    return key->scancode == SDL_SCANCODE_F11
        || ((key->mod & KMOD_ALT) && (key->scancode == SDL_SCANCODE_RETURN || key->scancode == SDL_SCANCODE_KP_ENTER));
}

/* Tab, pressed or released; these don't reach the game either. */
static bool IsFastForwardKey(const SDL_Event *event)
{
    return (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
        && event->key.keysym.scancode == SDL_SCANCODE_TAB;
}

void Platform_PollEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            sQuitRequested = true;
        if (IsFullscreenToggle(&event))
        {
            if (!event.key.repeat)
                ToggleFullscreen();
            continue;
        }
        if (IsFastForwardKey(&event))
        {
            if (event.type == SDL_KEYDOWN && !event.key.repeat)
                sFastForwardToggles++;
            continue;
        }
        Input_HandleEvent(&event);
    }
}

uint16_t Platform_GetButtons(void)
{
    return Input_GetButtons();
}

bool Platform_TakeFastForwardToggle(void)
{
    if (sFastForwardToggles == 0)
        return false;
    sFastForwardToggles--;
    return true;
}

bool Platform_QuitRequested(void)
{
    return sQuitRequested;
}

void Platform_RequestQuit(void)
{
    sQuitRequested = true;
}

uint64_t Platform_GetTimeNs(void)
{
    uint64_t ticks = SDL_GetPerformanceCounter() - sStartCounter;

    /* Split to avoid overflowing ticks * 1e9. */
    return (ticks / sCounterFrequency) * 1000000000u
         + (ticks % sCounterFrequency) * 1000000000u / sCounterFrequency;
}

uint32_t Platform_GetTicks(void)
{
    return (uint32_t)(Platform_GetTimeNs() / 1000000u);
}

void Platform_SleepMs(uint32_t ms)
{
    SDL_Delay(ms);
}

/* SDL_Delay only has millisecond resolution (and the OS may overshoot by
 * about a millisecond), so sleep for all but the last ~2 ms and spin for the
 * rest. Frames are ~16.7 ms, so this costs little CPU. */
void Platform_SleepNs(uint64_t ns)
{
    uint64_t deadline = Platform_GetTimeNs() + ns;

    if (ns > 2000000u)
        SDL_Delay((uint32_t)((ns - 2000000u) / 1000000u));
    while (Platform_GetTimeNs() < deadline)
        ;
}
