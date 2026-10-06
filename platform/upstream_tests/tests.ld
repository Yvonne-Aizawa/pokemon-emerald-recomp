/*
 * platform/upstream_tests/tests.ld
 *
 * Added to the default linker script for pkmemerald-tests: gathers the
 * TEST() records (section ".tests", include/test/test.h) and marks where they
 * start and stop, as ld_script_test.ld does for the test ROM. ld only makes
 * __start_/__stop_ symbols itself for sections named like C identifiers.
 */

SECTIONS
{
    .tests :
    {
        __start_tests = .;
        KEEP(*(.tests))
        __stop_tests = .;
    }
}
INSERT AFTER .rodata;
