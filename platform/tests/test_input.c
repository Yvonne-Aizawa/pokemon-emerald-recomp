/*
 * platform/tests/test_input.c
 *
 * Input path, end to end: synthetic SDL key events -> Platform_GetButtons
 * -> REG_KEYINPUT -> the game's own ReadKeys. The last check presses A
 * during the intro and expects the game to skip to the title screen.
 */

#include <SDL.h>
#include <stdio.h>

#include "global.h"
#include "intro.h"
#include "main.h"
#include "title_screen.h"

#include "platform/host_game.h"
#include "platform/host_input.h"
#include "platform/platform.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static void SendKey(SDL_Scancode scancode, bool down)
{
    SDL_Event event = {0};

    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = scancode;
    SDL_PushEvent(&event);
    Platform_PollEvents();
}

static void RunFrame(void)
{
    Platform_PollEvents();
    Host_SetKeypad(Platform_GetButtons());
    HostMain_RunFrame();
}

int main(void)
{
    struct PlatformConfig config = { .dataDir = "assets", .saveDir = "saves", .scale = 1 };
    SDL_Event focusLost = {0};
    bool reachedTitle = false;
    int frame;

    if (Platform_Init(&config) != 0)
    {
        Check(0, "Platform_Init");
        return 1;
    }

    /* Keyboard mapping. */
    Check(Platform_GetButtons() == 0, "no buttons held at start");
    SendKey(SDL_SCANCODE_Z, true);
    SendKey(SDL_SCANCODE_RETURN, true);
    Check(Platform_GetButtons() == (PLATFORM_BUTTON_A | PLATFORM_BUTTON_START), "Z = A, Enter = Start");
    SendKey(SDL_SCANCODE_Z, false);
    Check(Platform_GetButtons() == PLATFORM_BUTTON_START, "releasing Z releases A");
    SendKey(SDL_SCANCODE_RETURN, false);

    SendKey(SDL_SCANCODE_LSHIFT, true);
    SendKey(SDL_SCANCODE_RSHIFT, true);
    SendKey(SDL_SCANCODE_LSHIFT, false);
    Check(Platform_GetButtons() == PLATFORM_BUTTON_L, "L held while either Shift is down");
    SendKey(SDL_SCANCODE_RSHIFT, false);

    SendKey(SDL_SCANCODE_UP, true);
    focusLost.type = SDL_WINDOWEVENT;
    focusLost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&focusLost);
    Platform_PollEvents();
    Check(Platform_GetButtons() == 0, "losing focus releases held keys");

    /* Keypad register. */
    Host_SetKeypad(0);
    Check(REG_KEYINPUT == KEYS_MASK, "nothing held: KEYINPUT reads 0x3FF");
    Host_SetKeypad(PLATFORM_BUTTON_A | PLATFORM_BUTTON_UP);
    Check(REG_KEYINPUT == (KEYS_MASK & ~(A_BUTTON | DPAD_UP)), "held buttons read as 0 bits (active-low)");
    Host_SetKeypad(PLATFORM_BUTTON_LEFT | PLATFORM_BUTTON_RIGHT | PLATFORM_BUTTON_B);
    Check(REG_KEYINPUT == (KEYS_MASK & ~B_BUTTON), "opposite D-pad directions cancel");

    /* The game: press A in the intro, expect the title screen. */
    AgbMain();
    for (frame = 0; frame < 600 && gMain.callback2 != MainCB2_Intro; frame++)
        RunFrame();
    Check(gMain.callback2 == MainCB2_Intro, "game reaches the intro");
    for (frame = 0; frame < 120; frame++)
        RunFrame();
    Check(gMain.callback2 == MainCB2_Intro, "intro keeps playing with no input");

    SendKey(SDL_SCANCODE_Z, true);
    RunFrame();
    SendKey(SDL_SCANCODE_Z, false);
    for (frame = 0; frame < 300 && !reachedTitle; frame++)
    {
        RunFrame();
        reachedTitle = (gMain.callback2 == CB2_InitTitleScreen);
    }
    Check(reachedTitle, "pressing A skips the intro to the title screen");

    Platform_Shutdown();
    printf(sFailures ? "input: %d FAILED\n" : "input: all tests passed\n", sFailures);
    return sFailures != 0;
}
