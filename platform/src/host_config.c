/*
 * platform/src/host_config.c
 *
 * Player settings file (see platform/host_config.h).
 */

#include "platform/host_config.h"
#include "platform/host_fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_FILE_SIZE (64 * 1024)

const char *const gHostConfigButtonNames[PLATFORM_BUTTON_COUNT] = {
    "a", "b", "select", "start", "right", "left", "up", "down", "r", "l",
};

/* The order buttons appear in the default file. */
static const int sButtonFileOrder[PLATFORM_BUTTON_COUNT] = { 6, 7, 5, 4, 0, 1, 9, 8, 3, 2 };

void HostConfig_SetDefaults(struct HostConfig *config)
{
    int i;

    memset(config, 0, sizeof(*config));
    config->scale = 3;
    config->fullscreen = false;
    config->sound = true;
    config->smoothSound = false;
    for (i = 0; i < PLATFORM_BUTTON_COUNT; i++)
        snprintf(config->keys[i], sizeof(config->keys[i]), "%s", gPlatformDefaultKeys[i]);
}

/* --------------------------------------------------------------------- */
/* Parsing                                                               */
/* --------------------------------------------------------------------- */

static char *Trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static bool ParseBool(const char *value, bool *out)
{
    static const char *const sTrue[] = { "yes", "true", "on", "1" };
    static const char *const sFalse[] = { "no", "false", "off", "0" };
    size_t i;

    for (i = 0; i < sizeof(sTrue) / sizeof(sTrue[0]); i++)
    {
        if (strcasecmp(value, sTrue[i]) == 0)
        {
            *out = true;
            return true;
        }
        if (strcasecmp(value, sFalse[i]) == 0)
        {
            *out = false;
            return true;
        }
    }
    return false;
}

struct Parser
{
    struct HostConfig *config;
    const char *source;
    int line;
    int problems;
};

static void Problem(struct Parser *p, const char *format, const char *a, const char *b)
{
    fprintf(stderr, "config: %s:%d: ", p->source, p->line);
    fprintf(stderr, format, a, b);
    fprintf(stderr, "; using the default\n");
    p->problems++;
}

static void SetBool(struct Parser *p, const char *key, const char *value, bool *out)
{
    if (!ParseBool(value, out))
        Problem(p, "'%s' should be yes or no, not '%s'", key, value);
}

static void Apply(struct Parser *p, const char *section, const char *key, const char *value)
{
    struct HostConfig *c = p->config;
    int i;

    if (strcasecmp(section, "video") == 0)
    {
        if (strcasecmp(key, "scale") == 0)
        {
            char *end;
            long scale = strtol(value, &end, 10);

            if (*value == '\0' || *end != '\0' || scale < 1 || scale > HOST_CONFIG_MAX_SCALE)
                Problem(p, "'%s' should be a number from 1 to 16, not '%s'", key, value);
            else
                c->scale = (int)scale;
            return;
        }
        if (strcasecmp(key, "fullscreen") == 0)
        {
            SetBool(p, key, value, &c->fullscreen);
            return;
        }
    }
    else if (strcasecmp(section, "sound") == 0)
    {
        if (strcasecmp(key, "sound") == 0)
        {
            SetBool(p, key, value, &c->sound);
            return;
        }
        if (strcasecmp(key, "smooth_sound") == 0)
        {
            SetBool(p, key, value, &c->smoothSound);
            return;
        }
    }
    else if (strcasecmp(section, "keyboard") == 0)
    {
        for (i = 0; i < PLATFORM_BUTTON_COUNT; i++)
        {
            if (strcasecmp(key, gHostConfigButtonNames[i]) == 0)
            {
                if (strlen(value) >= sizeof(c->keys[i]))
                    Problem(p, "the key list for '%s' is too long%s", key, "");
                else
                    strcpy(c->keys[i], value);
                return;
            }
        }
    }
    Problem(p, "unknown setting '%s' in [%s]", key, section);
}

int HostConfig_Parse(struct HostConfig *config, const char *text, const char *sourceName)
{
    struct Parser p = { config, sourceName, 0, 0 };
    char section[64] = "";
    const char *cursor = text;

    while (*cursor != '\0')
    {
        const char *eol = strchr(cursor, '\n');
        size_t length = eol != NULL ? (size_t)(eol - cursor) : strlen(cursor);
        char buffer[512];
        char *line, *equals;

        p.line++;
        if (length >= sizeof(buffer))
        {
            Problem(&p, "line too long%s%s", "", "");
            cursor += length + (eol != NULL);
            continue;
        }
        memcpy(buffer, cursor, length);
        buffer[length] = '\0';
        cursor += length + (eol != NULL);

        /* Whole-line comments only: key names may contain '#' ("Keypad #"). */
        line = Trim(buffer);
        if (*line == '\0' || *line == '#' || *line == ';')
            continue;
        if (*line == '[')
        {
            char *close = strchr(line, ']');

            if (close == NULL || close[1] != '\0' || (size_t)(close - line - 1) >= sizeof(section))
            {
                Problem(&p, "malformed section header '%s'%s", line, "");
                continue;
            }
            *close = '\0';
            strcpy(section, Trim(line + 1));
            /* Reported once here; its settings are then skipped quietly. */
            if (strcasecmp(section, "video") != 0 && strcasecmp(section, "sound") != 0
             && strcasecmp(section, "keyboard") != 0)
            {
                Problem(&p, "unknown section [%s]%s", section, "");
                strcpy(section, "?");
            }
            continue;
        }
        equals = strchr(line, '=');
        if (equals == NULL)
        {
            Problem(&p, "expected 'setting = value', got '%s'%s", line, "");
            continue;
        }
        *equals = '\0';
        if (section[0] == '\0')
        {
            Problem(&p, "'%s' is outside any [section]%s", Trim(line), "");
            continue;
        }
        if (strcmp(section, "?") == 0)
            continue;
        Apply(&p, section, Trim(line), Trim(equals + 1));
    }
    return p.problems;
}

/* --------------------------------------------------------------------- */
/* The default file                                                       */
/* --------------------------------------------------------------------- */

const char *HostConfig_DefaultText(void)
{
    static char sText[4096];
    struct HostConfig defaults;
    size_t length;
    int i;

    if (sText[0] != '\0')
        return sText;
    HostConfig_SetDefaults(&defaults);
    length = (size_t)snprintf(sText, sizeof(sText),
        "# pkmemerald settings\n"
        "#\n"
        "# Written with the defaults when the game first starts; the game never\n"
        "# changes this file after that. Command-line flags override it (see\n"
        "# pkmemerald --help). Delete the file to get the defaults back.\n"
        "\n"
        "[video]\n"
        "# Initial window size, as a multiple of the GBA's 240x160 (1-16).\n"
        "scale = %d\n"
        "# Start in fullscreen (yes/no). F11 or Alt+Enter switch while playing.\n"
        "fullscreen = %s\n"
        "\n"
        "[sound]\n"
        "sound = %s\n"
        "# yes: interpolated and low-passed; no: the GBA's exact output.\n"
        "smooth_sound = %s\n"
        "\n"
        "[keyboard]\n"
        "# Keys for each GBA button, separated by commas; leave empty for none.\n"
        "# Keys go by position, so \"Z\" means the key right of Left Shift on any\n"
        "# keyboard layout. Key names: A ... Z, 0 ... 9, Space, Return, Tab,\n"
        "# Escape, Backspace, Left Shift, Right Shift, Left Ctrl, Right Ctrl,\n"
        "# Left Alt, Up, Down, Left, Right, F1 ... F10, F12 (F11 switches\n"
        "# fullscreen), Keypad 0 ... Keypad 9, Keypad Enter. Unknown names are\n"
        "# reported in the log, and that button keeps its default keys.\n"
        "# Gamepads use the standard layout and aren't configured here.\n",
        defaults.scale, defaults.fullscreen ? "yes" : "no",
        defaults.sound ? "yes" : "no", defaults.smoothSound ? "yes" : "no");
    for (i = 0; i < PLATFORM_BUTTON_COUNT && length < sizeof(sText); i++)
    {
        int button = sButtonFileOrder[i];

        length += (size_t)snprintf(sText + length, sizeof(sText) - length, "%s = %s\n",
                                   gHostConfigButtonNames[button], defaults.keys[button]);
    }
    return sText;
}

/* --------------------------------------------------------------------- */
/* Loading                                                                */
/* --------------------------------------------------------------------- */

bool HostConfig_Load(struct HostConfig *config, const char *path)
{
    FILE *file;
    char *text;
    size_t size;
    char error[1024];

    HostConfig_SetDefaults(config);
    file = fopen(path, "rb");
    if (file == NULL)
    {
        const char *defaults = HostConfig_DefaultText();

        if (errno != ENOENT)
        {
            fprintf(stderr, "config: cannot read '%s': %s; using the defaults\n", path, strerror(errno));
            return false;
        }
        if (HostFs_WriteFileAtomic(path, defaults, strlen(defaults), error, sizeof(error)))
            printf("config: %s (created with the defaults)\n", path);
        else
            fprintf(stderr, "config: %s; using the defaults\n", error);
        return true;
    }

    text = malloc(MAX_FILE_SIZE + 1);
    size = text != NULL ? fread(text, 1, MAX_FILE_SIZE + 1, file) : 0;
    fclose(file);
    if (text == NULL || size > MAX_FILE_SIZE)
    {
        fprintf(stderr, "config: '%s' is too large; using the defaults\n", path);
        free(text);
        return false;
    }
    text[size] = '\0';
    printf("config: %s\n", path);
    fflush(stdout);  /* before any problems, which go to stderr */
    HostConfig_Parse(config, text, path);
    free(text);
    return true;
}
