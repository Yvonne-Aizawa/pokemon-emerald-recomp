#ifndef PLATFORM_HOST_SAVE_ABI_H
#define PLATFORM_HOST_SAVE_ABI_H
#include <stdbool.h>
#include <stddef.h>
/* Raw .sav files retain the 32-bit save layout. Internal flash follows the
 * native structs; JSON names fields and is independent of byte offsets. */
bool HostSave_ConvertFlashAbi(unsigned char *flash, bool toNative, char *error, size_t errorSize);
#endif
