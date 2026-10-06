/*
 * platform/tests/test_config.c
 *
 * The settings file (platform/host_config.h): defaults, parsing, mistakes
 * falling back to the default, and creating the default file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <process.h>
#endif

#include "platform/host_config.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static void TempPath(char *out, size_t size)
{
#ifdef _WIN32
    const char *dir = getenv("TEMP");
    int pid = _getpid();
#else
    const char *dir = "/tmp";
    int pid = getpid();
#endif
    snprintf(out, size, "%s/pkmemerald-test-config-%d.ini", dir != NULL ? dir : ".", pid);
}

int main(void)
{
    struct HostConfig config, defaults;
    char path[512];
    FILE *f;
    int problems;

    HostConfig_SetDefaults(&defaults);
    Check(defaults.scale == 3 && !defaults.fullscreen && defaults.sound && !defaults.smoothSound,
          "defaults: scale 3, windowed, sound on, exact sound");
    Check(strcmp(defaults.keys[0], "Z") == 0 && strcmp(defaults.keys[9], "Left Shift, Right Shift") == 0,
          "default keys come from the platform layout");

    /* The default file parses back to the defaults, with no problems. */
    config = defaults;
    config.scale = 7;
    problems = HostConfig_Parse(&config, HostConfig_DefaultText(), "default");
    Check(problems == 0 && memcmp(&config, &defaults, sizeof(config)) == 0, "default file parses to the defaults");

    /* Every setting, in any case, with CRLF line ends and comments. */
    HostConfig_SetDefaults(&config);
    problems = HostConfig_Parse(&config,
        "# comment\r\n"
        "; also a comment\r\n"
        "[Video]\r\n"
        "  Scale =  5  \r\n"
        "fullscreen = YES\r\n"
        "[sound]\r\n"
        "sound = off\r\n"
        "smooth_sound = 1\r\n"
        "[keyboard]\r\n"
        "a = Space\r\n"
        "L =\r\n"
        "select = Keypad #\r\n",
        "test");
    Check(problems == 0, "valid file: no problems");
    Check(config.scale == 5 && config.fullscreen && !config.sound && config.smoothSound, "video and sound settings applied");
    Check(strcmp(config.keys[0], "Space") == 0, "a = Space");
    Check(config.keys[9][0] == '\0', "l = (empty)");
    Check(strcmp(config.keys[2], "Keypad #") == 0, "'#' inside a value isn't a comment");
    Check(strcmp(config.keys[1], "X") == 0, "unset keys keep their defaults");

    /* Mistakes: each is reported and leaves the default. */
    HostConfig_SetDefaults(&config);
    problems = HostConfig_Parse(&config,
        "scale = 4\n"            /* outside a section */
        "[video]\n"
        "scale = 0\n"            /* out of range */
        "scale = 17\n"
        "scale = big\n"
        "fullscreen = maybe\n"   /* not a yes/no */
        "zoom = 2\n"             /* unknown setting */
        "just some words\n"      /* no '=' */
        "[graphics\n"            /* malformed header */
        "[network]\n"            /* unknown section: reported once */
        "port = 1\n"
        "host = example\n",
        "test");
    Check(problems == 9, "nine mistakes, nine problems reported");
    Check(memcmp(&config, &defaults, sizeof(config)) == 0, "mistakes leave every setting at its default");

    /* Loading: a missing file is created with the defaults; then read back. */
    TempPath(path, sizeof(path));
    remove(path);
    Check(HostConfig_Load(&config, path), "load with no file");
    Check(memcmp(&config, &defaults, sizeof(config)) == 0, "no file: defaults");
    f = fopen(path, "rb");
    Check(f != NULL, "the default file was created");
    if (f != NULL)
        fclose(f);

    f = fopen(path, "ab");
    if (f != NULL)
    {
        fputs("[video]\nscale = 2\n", f);
        fclose(f);
    }
    Check(HostConfig_Load(&config, path) && config.scale == 2, "an edited file is read back (later lines win)");
    remove(path);

    printf(sFailures ? "config: %d FAILED\n" : "config: all tests passed\n", sFailures);
    return sFailures != 0;
}
