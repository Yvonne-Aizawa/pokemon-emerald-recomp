/*
 * platform/src/host_input.c
 *
 * The GBA keypad; see platform/host_input.h.
 *
 * The game reads REG_KEYINPUT itself once per frame (ReadKeys in main.c),
 * including key repeat and the L=A option, so all the host does is present
 * the held buttons in that register's format.
 */

#include "platform/host_input.h"
#include "platform/platform.h"

#include "global.h"

void Host_SetKeypad(uint16_t buttons)
{
    /* A real D-pad can't press opposite directions together, and game code
     * isn't written to expect it; a keyboard can, so cancel such pairs. */
    if ((buttons & (DPAD_LEFT | DPAD_RIGHT)) == (DPAD_LEFT | DPAD_RIGHT))
        buttons &= ~(DPAD_LEFT | DPAD_RIGHT);
    if ((buttons & (DPAD_UP | DPAD_DOWN)) == (DPAD_UP | DPAD_DOWN))
        buttons &= ~(DPAD_UP | DPAD_DOWN);

    /* Active-low: a set bit means "not pressed". */
    REG_KEYINPUT = KEYS_MASK & ~buttons;
}

/* The platform layer uses the GBA's bit layout; make sure it stays so. */
_Static_assert(PLATFORM_BUTTON_A == A_BUTTON && PLATFORM_BUTTON_B == B_BUTTON
            && PLATFORM_BUTTON_SELECT == SELECT_BUTTON && PLATFORM_BUTTON_START == START_BUTTON
            && PLATFORM_BUTTON_RIGHT == DPAD_RIGHT && PLATFORM_BUTTON_LEFT == DPAD_LEFT
            && PLATFORM_BUTTON_UP == DPAD_UP && PLATFORM_BUTTON_DOWN == DPAD_DOWN
            && PLATFORM_BUTTON_R == R_BUTTON && PLATFORM_BUTTON_L == L_BUTTON,
               "PLATFORM_BUTTON_* must match the GBA's KEYINPUT bits");
