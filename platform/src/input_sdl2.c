/*
 * platform/src/input_sdl2.c
 *
 * Keyboard and gamepad -> GBA buttons.
 *
 * Keyboard (by physical key position, so it works on any layout), by
 * default (gPlatformDefaultKeys; the player can change it in the settings
 * file, see platform/host_config.h):
 *   Z = A, X = B, Enter = Start, Backspace = Select, arrows = D-pad,
 *   Shift = L, Ctrl = R.
 * Gamepad (SDL's standard controller layout, i.e. Xbox naming):
 *   A = A, B = B, Back = Select, Start = Start, D-pad or left stick = D-pad,
 *   shoulder buttons or triggers = L / R.
 *
 * Keyboard state is tracked from key events (not SDL_GetKeyboardState) so
 * synthetic events, e.g. from tests, behave like real key presses.
 *
 * Presses are latched until the next Input_GetButtons: events are drained
 * once a frame, so a quick tap's key-down and key-up often arrive together,
 * and without the latch the game would never see the button held.
 */

#include "input_sdl2.h"

#include "platform/platform.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_GAMEPADS 8
#define MAX_KEY_BINDINGS 64

/* Stick and trigger thresholds, out of 32767. */
#define STICK_DEADZONE   16000
#define TRIGGER_DEADZONE 16000

struct KeyBinding
{
    SDL_Scancode scancode;
    uint16_t button;
};

/* SDL scancode names (SDL_GetScancodeName), indexed by button bit. */
const char *const gPlatformDefaultKeys[PLATFORM_BUTTON_COUNT] = {
    "Z",                       /* A */
    "X",                       /* B */
    "Backspace",               /* Select */
    "Return, Keypad Enter",    /* Start */
    "Right",
    "Left",
    "Up",
    "Down",
    "Left Ctrl, Right Ctrl",   /* R */
    "Left Shift, Right Shift", /* L */
};

static struct KeyBinding sKeyBindings[MAX_KEY_BINDINGS];
static size_t sKeyBindingCount;

struct PadBinding
{
    SDL_GameControllerButton sdlButton;
    uint16_t button;
};

static const struct PadBinding sPadBindings[] = {
    { SDL_CONTROLLER_BUTTON_A,             PLATFORM_BUTTON_A },
    { SDL_CONTROLLER_BUTTON_B,             PLATFORM_BUTTON_B },
    { SDL_CONTROLLER_BUTTON_BACK,          PLATFORM_BUTTON_SELECT },
    { SDL_CONTROLLER_BUTTON_START,         PLATFORM_BUTTON_START },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    PLATFORM_BUTTON_RIGHT },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     PLATFORM_BUTTON_LEFT },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       PLATFORM_BUTTON_UP },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     PLATFORM_BUTTON_DOWN },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  PLATFORM_BUTTON_L },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PLATFORM_BUTTON_R },
};

/* Per-scancode held state, so two keys bound to one button (both Shifts)
 * don't release it when only one is let go. */
static bool sKeyHeld[SDL_NUM_SCANCODES];
/* Buttons pressed since the last Input_GetButtons, held or not. */
static uint16_t sPressedSinceSample;
static SDL_GameController *sGamepads[MAX_GAMEPADS];

static void OpenGamepad(int deviceIndex)
{
    int i;

    if (!SDL_IsGameController(deviceIndex))
        return;
    for (i = 0; i < MAX_GAMEPADS; i++)
    {
        if (sGamepads[i] == NULL)
        {
            sGamepads[i] = SDL_GameControllerOpen(deviceIndex);
            return;
        }
    }
}

static void CloseGamepad(SDL_JoystickID instanceId)
{
    int i;

    for (i = 0; i < MAX_GAMEPADS; i++)
    {
        if (sGamepads[i] != NULL
         && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sGamepads[i])) == instanceId)
        {
            SDL_GameControllerClose(sGamepads[i]);
            sGamepads[i] = NULL;
        }
    }
}

/* Add the keys in `names` ("Left Shift, Right Shift") for `button`. Unknown
 * names are reported and skipped; returns false if there were any. */
static bool AddKeys(const char *names, uint16_t button, const char *buttonName)
{
    char buffer[256];
    char *name, *save;
    bool ok = true;

    snprintf(buffer, sizeof(buffer), "%s", names);
    for (name = strtok_r(buffer, ",", &save); name != NULL; name = strtok_r(NULL, ",", &save))
    {
        char *end = name + strlen(name);
        SDL_Scancode scancode;

        while (*name == ' ' || *name == '\t')
            name++;
        while (end > name && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        if (*name == '\0')
            continue;
        scancode = SDL_GetScancodeFromName(name);
        if (scancode == SDL_SCANCODE_UNKNOWN)
        {
            fprintf(stderr, "config: unknown key '%s' for button %s\n", name, buttonName);
            ok = false;
            continue;
        }
        if (sKeyBindingCount == MAX_KEY_BINDINGS)
        {
            fprintf(stderr, "config: too many keys; '%s' for button %s ignored\n", name, buttonName);
            ok = false;
            continue;
        }
        sKeyBindings[sKeyBindingCount].scancode = scancode;
        sKeyBindings[sKeyBindingCount].button = button;
        sKeyBindingCount++;
    }
    return ok;
}

void Input_Init(const char *const keys[PLATFORM_BUTTON_COUNT])
{
    static const char *const sButtonNames[PLATFORM_BUTTON_COUNT] = {
        "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L",
    };
    int i;

    /* Gamepads already plugged in arrive as SDL_CONTROLLERDEVICEADDED
     * events on the first poll. */
    memset(sKeyHeld, 0, sizeof(sKeyHeld));
    sPressedSinceSample = 0;
    memset(sGamepads, 0, sizeof(sGamepads));

    /* A list with any unknown key falls back to the default as a whole, so
     * a typo can't leave a button unreachable. An empty list is allowed:
     * gamepad only. */
    sKeyBindingCount = 0;
    for (i = 0; i < PLATFORM_BUTTON_COUNT; i++)
    {
        size_t before = sKeyBindingCount;
        const char *names = (keys != NULL && keys[i] != NULL) ? keys[i] : gPlatformDefaultKeys[i];

        if (!AddKeys(names, (uint16_t)(1u << i), sButtonNames[i]))
        {
            sKeyBindingCount = before;
            fprintf(stderr, "config: button %s uses its default keys (%s)\n", sButtonNames[i], gPlatformDefaultKeys[i]);
            AddKeys(gPlatformDefaultKeys[i], (uint16_t)(1u << i), sButtonNames[i]);
        }
    }
}

void Input_Shutdown(void)
{
    int i;

    for (i = 0; i < MAX_GAMEPADS; i++)
    {
        if (sGamepads[i] != NULL)
            SDL_GameControllerClose(sGamepads[i]);
        sGamepads[i] = NULL;
    }
}

static uint16_t KeyButtons(SDL_Scancode scancode)
{
    uint16_t buttons = 0;
    size_t i;

    for (i = 0; i < sKeyBindingCount; i++)
    {
        if (sKeyBindings[i].scancode == scancode)
            buttons |= sKeyBindings[i].button;
    }
    return buttons;
}

static uint16_t PadButton(Uint8 sdlButton)
{
    size_t i;

    for (i = 0; i < SDL_arraysize(sPadBindings); i++)
    {
        if (sPadBindings[i].sdlButton == sdlButton)
            return sPadBindings[i].button;
    }
    return 0;
}

void Input_HandleEvent(const SDL_Event *event)
{
    switch (event->type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event->key.keysym.scancode < SDL_NUM_SCANCODES)
            sKeyHeld[event->key.keysym.scancode] = (event->type == SDL_KEYDOWN);
        if (event->type == SDL_KEYDOWN && !event->key.repeat)
            sPressedSinceSample |= KeyButtons(event->key.keysym.scancode);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        sPressedSinceSample |= PadButton(event->cbutton.button);
        break;
    case SDL_WINDOWEVENT:
        /* Key-up events for keys released while unfocused never arrive. */
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        {
            memset(sKeyHeld, 0, sizeof(sKeyHeld));
            sPressedSinceSample = 0;
        }
        break;
    case SDL_CONTROLLERDEVICEADDED:
        OpenGamepad(event->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        CloseGamepad(event->cdevice.which);
        break;
    }
}

static uint16_t GamepadButtons(SDL_GameController *pad)
{
    uint16_t buttons = 0;
    Sint16 x, y;
    size_t i;

    for (i = 0; i < SDL_arraysize(sPadBindings); i++)
    {
        if (SDL_GameControllerGetButton(pad, sPadBindings[i].sdlButton))
            buttons |= sPadBindings[i].button;
    }

    x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (x > STICK_DEADZONE)
        buttons |= PLATFORM_BUTTON_RIGHT;
    if (x < -STICK_DEADZONE)
        buttons |= PLATFORM_BUTTON_LEFT;
    if (y > STICK_DEADZONE)
        buttons |= PLATFORM_BUTTON_DOWN;
    if (y < -STICK_DEADZONE)
        buttons |= PLATFORM_BUTTON_UP;

    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_DEADZONE)
        buttons |= PLATFORM_BUTTON_L;
    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_DEADZONE)
        buttons |= PLATFORM_BUTTON_R;

    return buttons;
}

uint16_t Input_GetButtons(void)
{
    uint16_t buttons = sPressedSinceSample;
    size_t i;

    sPressedSinceSample = 0;
    for (i = 0; i < sKeyBindingCount; i++)
    {
        if (sKeyHeld[sKeyBindings[i].scancode])
            buttons |= sKeyBindings[i].button;
    }
    for (i = 0; i < MAX_GAMEPADS; i++)
    {
        if (sGamepads[i] != NULL)
            buttons |= GamepadButtons(sGamepads[i]);
    }
    return buttons;
}
