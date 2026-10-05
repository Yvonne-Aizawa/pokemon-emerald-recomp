/*
 * platform/include/platform/host_input.h
 *
 * The GBA's keypad, emulated on the host.
 */

#ifndef PLATFORM_HOST_INPUT_H
#define PLATFORM_HOST_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set the keypad state the game reads (REG_KEYINPUT) from held buttons
 * (PLATFORM_BUTTON_* bits). Call once per frame, before the game runs. */
void Host_SetKeypad(uint16_t buttons);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_INPUT_H */
