/*
 * platform/src/input_sdl2.c
 *
 * Keyboard and gamepad -> GBA buttons.
 *
 * Keyboard (by physical key position, so it works on any layout):
 *   Z = A, X = B, Enter = Start, Backspace = Select, arrows = D-pad,
 *   Shift = L, Ctrl = R.
 * Gamepad (SDL's standard controller layout, i.e. Xbox naming):
 *   A = A, B = B, Back = Select, Start = Start, D-pad or left stick = D-pad,
 *   shoulder buttons or triggers = L / R.
 *
 * Keyboard state is tracked from key events (not SDL_GetKeyboardState) so
 * synthetic events, e.g. from tests, behave like real key presses.
 */

#include "input_sdl2.h"

#include "platform/platform.h"

#include <stdbool.h>
#include <string.h>

#define MAX_GAMEPADS 8

/* Stick and trigger thresholds, out of 32767. */
#define STICK_DEADZONE   16000
#define TRIGGER_DEADZONE 16000

struct KeyBinding
{
    SDL_Scancode scancode;
    uint16_t button;
};

static const struct KeyBinding sKeyBindings[] = {
    { SDL_SCANCODE_Z,         PLATFORM_BUTTON_A },
    { SDL_SCANCODE_X,         PLATFORM_BUTTON_B },
    { SDL_SCANCODE_RETURN,    PLATFORM_BUTTON_START },
    { SDL_SCANCODE_KP_ENTER,  PLATFORM_BUTTON_START },
    { SDL_SCANCODE_BACKSPACE, PLATFORM_BUTTON_SELECT },
    { SDL_SCANCODE_RIGHT,     PLATFORM_BUTTON_RIGHT },
    { SDL_SCANCODE_LEFT,      PLATFORM_BUTTON_LEFT },
    { SDL_SCANCODE_UP,        PLATFORM_BUTTON_UP },
    { SDL_SCANCODE_DOWN,      PLATFORM_BUTTON_DOWN },
    { SDL_SCANCODE_LSHIFT,    PLATFORM_BUTTON_L },
    { SDL_SCANCODE_RSHIFT,    PLATFORM_BUTTON_L },
    { SDL_SCANCODE_LCTRL,     PLATFORM_BUTTON_R },
    { SDL_SCANCODE_RCTRL,     PLATFORM_BUTTON_R },
};

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

void Input_Init(void)
{
    /* Gamepads already plugged in arrive as SDL_CONTROLLERDEVICEADDED
     * events on the first poll. */
    memset(sKeyHeld, 0, sizeof(sKeyHeld));
    memset(sGamepads, 0, sizeof(sGamepads));
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

void Input_HandleEvent(const SDL_Event *event)
{
    switch (event->type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event->key.keysym.scancode < SDL_NUM_SCANCODES)
            sKeyHeld[event->key.keysym.scancode] = (event->type == SDL_KEYDOWN);
        break;
    case SDL_WINDOWEVENT:
        /* Key-up events for keys released while unfocused never arrive. */
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            memset(sKeyHeld, 0, sizeof(sKeyHeld));
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
    uint16_t buttons = 0;
    size_t i;

    for (i = 0; i < SDL_arraysize(sKeyBindings); i++)
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
