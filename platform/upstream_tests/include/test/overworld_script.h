/* Host version of upstream's include/test/overworld_script.h (PLAN.md,
 * Phase 19b): build/host_include/test/ links to it instead. Everything is as
 * upstream (see its file for how to use it) except:
 *  - Upstream writes the script into the code with ARM assembly (`mov %0,
 *    pc`, `b 1f`, the script, `1:`). Here it goes into .rodata -- ROM to the
 *    game (Host_IsGbaRamPointer), as code is on the GBA -- and its address is
 *    taken with a mov.
 *  - The script macros are host copies of upstream's (host_assemble.sh inc;
 *    CMakeLists.txt: HOST_TEST_ASM_DIR). They are defined for the whole
 *    file, the C code's too, so the ones named like x86 instructions (call,
 *    inc, nop, lock) are host_call etc. there; each script gets the upstream
 *    names as aliases, removed again after it. */
#ifndef GUARD_TEST_OVERWORLD_SCRIPT
#define GUARD_TEST_OVERWORLD_SCRIPT

#include "script.h"
#include "test/test.h"

#define HOST_SCRIPT_ALIAS(name) \
    ".macro " #name " args:vararg\nhost_" #name " \\args\n.endm\n"
#define HOST_SCRIPT_ALIASES \
    HOST_SCRIPT_ALIAS(call) HOST_SCRIPT_ALIAS(inc) HOST_SCRIPT_ALIAS(nop) HOST_SCRIPT_ALIAS(lock)
#define HOST_SCRIPT_UNALIASES \
    ".purgem call\n.purgem inc\n.purgem nop\n.purgem lock\n"

#define OVERWORLD_SCRIPT(...) \
    ({ \
        const u8 *_script; \
        asm(".pushsection .rodata\n" \
            HOST_SCRIPT_ALIASES \
            "2:\n" \
            STR(__VA_ARGS__) \
            "\n" \
            "end\n" \
            HOST_SCRIPT_UNALIASES \
            ".popsection\n" \
            "mov $2b, %0\n" \
        : "=r" (_script)); \
        _script; \
    })

#define RUN_OVERWORLD_SCRIPT(...) RunScriptImmediately(OVERWORLD_SCRIPT(__VA_ARGS__))

// Make important constants available.
// TODO: Find a better approach to this.
asm(".set FALSE, 0\n"
    ".set TRUE, 1\n"
    ".set PARTY_SIZE, " STR(PARTY_SIZE) "\n"
    ".set VARS_START, " STR(VARS_START) "\n"
    ".set VARS_END, " STR(VARS_END) "\n"
    ".set MON_GENDER_MAY_CUTE_CHARM, " STR(MON_GENDER_MAY_CUTE_CHARM) "\n"
    ".set NATURE_MAY_SYNCHRONIZE, " STR(NATURE_MAY_SYNCHRONIZE) "\n"
    ".set SPECIAL_VARS_START, " STR(SPECIAL_VARS_START) "\n"
    ".set SPECIAL_VARS_END, " STR(SPECIAL_VARS_END) "\n");

// Make overworld script macros available.
asm(".include \"constants/gba_constants.inc\"\n"
    ".include \"asm/macros/asm.inc\"\n"
    ".include \"asm/macros/event.inc\"\n");

#endif
