/*
 * platform/src/host_json.c
 *
 * JSON reader and writer (see platform/host_json.h).
 */

#include "platform/host_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

/* --------------------------------------------------------------------- */
/* Reading                                                               */
/* --------------------------------------------------------------------- */

struct Parser
{
    const char *text;
    size_t length;
    size_t pos;
    char *error;
    size_t errorSize;
    bool failed;
};

static bool Fail(struct Parser *p, const char *what)
{
    if (!p->failed)
        snprintf(p->error, p->errorSize, "%s at byte %lu", what, (unsigned long)p->pos);
    p->failed = true;
    return false;
}

static void SkipSpace(struct Parser *p)
{
    while (p->pos < p->length)
    {
        char c = p->text[p->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        p->pos++;
    }
}

static bool Literal(struct Parser *p, const char *word)
{
    size_t n = strlen(word);
    if (p->length - p->pos < n || memcmp(p->text + p->pos, word, n) != 0)
        return Fail(p, "invalid literal");
    p->pos += n;
    return true;
}

static int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ReadHex4(struct Parser *p, unsigned *out)
{
    unsigned value = 0;
    int i;

    if (p->length - p->pos < 4)
        return Fail(p, "truncated \\u escape");
    for (i = 0; i < 4; i++)
    {
        int d = HexDigit(p->text[p->pos + i]);
        if (d < 0)
            return Fail(p, "invalid \\u escape");
        value = value * 16 + (unsigned)d;
    }
    p->pos += 4;
    *out = value;
    return true;
}

static size_t EncodeUtf8(unsigned cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Reads a string at p->pos (the opening quote) into a malloc'd buffer. The
 * decoded text is never longer than the source, which bounds the buffer. */
static bool ParseString(struct Parser *p, char **out, size_t *outLength)
{
    size_t start = ++p->pos;
    size_t end = start;
    char *buffer;
    size_t n = 0;

    while (end < p->length && p->text[end] != '"')
        end += p->text[end] == '\\' ? 2 : 1;
    if (end >= p->length)
        return Fail(p, "unterminated string");
    buffer = malloc(end - start + 1);
    if (buffer == NULL)
        return Fail(p, "out of memory");

    while (p->pos < end)
    {
        unsigned char c = (unsigned char)p->text[p->pos];
        if (c < 0x20)
        {
            free(buffer);
            return Fail(p, "control character in string");
        }
        if (c != '\\')
        {
            buffer[n++] = (char)c;
            p->pos++;
            continue;
        }
        p->pos++;
        switch (p->text[p->pos++])
        {
        case '"':  buffer[n++] = '"'; break;
        case '\\': buffer[n++] = '\\'; break;
        case '/':  buffer[n++] = '/'; break;
        case 'b':  buffer[n++] = '\b'; break;
        case 'f':  buffer[n++] = '\f'; break;
        case 'n':  buffer[n++] = '\n'; break;
        case 'r':  buffer[n++] = '\r'; break;
        case 't':  buffer[n++] = '\t'; break;
        case 'u':
        {
            unsigned cp, low;
            if (!ReadHex4(p, &cp))
            {
                free(buffer);
                return false;
            }
            if (cp >= 0xD800 && cp < 0xDC00)
            {
                /* A surrogate pair: 12 source bytes become 4. */
                if (p->pos + 2 > end || p->text[p->pos] != '\\' || p->text[p->pos + 1] != 'u')
                {
                    free(buffer);
                    return Fail(p, "unpaired surrogate");
                }
                p->pos += 2;
                if (!ReadHex4(p, &low) || low < 0xDC00 || low >= 0xE000)
                {
                    free(buffer);
                    return Fail(p, "unpaired surrogate");
                }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            }
            else if (cp >= 0xDC00 && cp < 0xE000)
            {
                free(buffer);
                return Fail(p, "unpaired surrogate");
            }
            n += EncodeUtf8(cp, buffer + n);
            break;
        }
        default:
            free(buffer);
            p->pos--;
            return Fail(p, "invalid escape");
        }
    }
    p->pos = end + 1;
    buffer[n] = '\0';
    *out = buffer;
    *outLength = n;
    return true;
}

static bool ParseNumber(struct Parser *p, double *out)
{
    size_t start = p->pos;
    bool whole = true;
    char copy[64];

    if (p->pos < p->length && p->text[p->pos] == '-')
        p->pos++;
    if (p->pos >= p->length || p->text[p->pos] < '0' || p->text[p->pos] > '9')
        return Fail(p, "invalid number");
    if (p->text[p->pos] == '0')
        p->pos++;
    else
        while (p->pos < p->length && p->text[p->pos] >= '0' && p->text[p->pos] <= '9')
            p->pos++;
    if (p->pos < p->length && p->text[p->pos] == '.')
    {
        whole = false;
        p->pos++;
        if (p->pos >= p->length || p->text[p->pos] < '0' || p->text[p->pos] > '9')
            return Fail(p, "invalid number");
        while (p->pos < p->length && p->text[p->pos] >= '0' && p->text[p->pos] <= '9')
            p->pos++;
    }
    if (p->pos < p->length && (p->text[p->pos] == 'e' || p->text[p->pos] == 'E'))
    {
        whole = false;
        p->pos++;
        if (p->pos < p->length && (p->text[p->pos] == '+' || p->text[p->pos] == '-'))
            p->pos++;
        if (p->pos >= p->length || p->text[p->pos] < '0' || p->text[p->pos] > '9')
            return Fail(p, "invalid number");
        while (p->pos < p->length && p->text[p->pos] >= '0' && p->text[p->pos] <= '9')
            p->pos++;
    }
    if (whole)
    {
        /* Exact for every integer the save uses; strtod would depend on the
         * C locale's decimal point. */
        size_t i = start + (p->text[start] == '-');
        double value = 0;
        for (; i < p->pos; i++)
            value = value * 10 + (p->text[i] - '0');
        *out = p->text[start] == '-' ? -value : value;
        return true;
    }
    if (p->pos - start >= sizeof(copy))
        return Fail(p, "number too long");
    memcpy(copy, p->text + start, p->pos - start);
    copy[p->pos - start] = '\0';
    *out = strtod(copy, NULL);
    return true;
}

static void FreeContents(struct HostJsonValue *v)
{
    size_t i;

    for (i = 0; i < v->count; i++)
        FreeContents(&v->items[i]);
    free(v->items);
    free(v->string);
    free(v->key);
}

static bool ParseValue(struct Parser *p, struct HostJsonValue *v, int depth);

static bool AppendItem(struct Parser *p, struct HostJsonValue *container, size_t *capacity, struct HostJsonValue **item)
{
    if (container->count == *capacity)
    {
        size_t newCapacity = *capacity != 0 ? *capacity * 2 : 8;
        struct HostJsonValue *items = realloc(container->items, newCapacity * sizeof(*items));
        if (items == NULL)
            return Fail(p, "out of memory");
        container->items = items;
        *capacity = newCapacity;
    }
    *item = &container->items[container->count++];
    memset(*item, 0, sizeof(**item));
    return true;
}

static bool ParseContainer(struct Parser *p, struct HostJsonValue *v, int depth)
{
    bool isObject = p->text[p->pos] == '{';
    char close = isObject ? '}' : ']';
    size_t capacity = 0;

    if (depth >= MAX_DEPTH)
        return Fail(p, "nested too deeply");
    v->type = isObject ? HOST_JSON_OBJECT : HOST_JSON_ARRAY;
    p->pos++;
    SkipSpace(p);
    if (p->pos < p->length && p->text[p->pos] == close)
    {
        p->pos++;
        return true;
    }
    for (;;)
    {
        struct HostJsonValue *item = NULL;

        if (!AppendItem(p, v, &capacity, &item))
            return false;
        if (isObject)
        {
            SkipSpace(p);
            if (p->pos >= p->length || p->text[p->pos] != '"')
                return Fail(p, "expected a member name");
            if (!ParseString(p, &item->key, &item->keyLength))
                return false;
            SkipSpace(p);
            if (p->pos >= p->length || p->text[p->pos] != ':')
                return Fail(p, "expected ':'");
            p->pos++;
        }
        if (!ParseValue(p, item, depth + 1))
            return false;
        SkipSpace(p);
        if (p->pos < p->length && p->text[p->pos] == ',')
        {
            p->pos++;
            continue;
        }
        if (p->pos < p->length && p->text[p->pos] == close)
        {
            p->pos++;
            return true;
        }
        return Fail(p, isObject ? "expected ',' or '}'" : "expected ',' or ']'");
    }
}

static bool ParseValue(struct Parser *p, struct HostJsonValue *v, int depth)
{
    SkipSpace(p);
    if (p->pos >= p->length)
        return Fail(p, "unexpected end of input");
    switch (p->text[p->pos])
    {
    case '{':
    case '[':
        return ParseContainer(p, v, depth);
    case '"':
        v->type = HOST_JSON_STRING;
        return ParseString(p, &v->string, &v->length);
    case 't':
        v->type = HOST_JSON_BOOL;
        v->boolean = true;
        return Literal(p, "true");
    case 'f':
        v->type = HOST_JSON_BOOL;
        return Literal(p, "false");
    case 'n':
        v->type = HOST_JSON_NULL;
        return Literal(p, "null");
    default:
        v->type = HOST_JSON_NUMBER;
        return ParseNumber(p, &v->number);
    }
}

struct HostJsonValue *HostJson_Parse(const char *text, size_t length, char *error, size_t errorSize)
{
    struct Parser p = { text, length, 0, error, errorSize, false };
    struct HostJsonValue *root = calloc(1, sizeof(*root));

    if (root == NULL)
    {
        snprintf(error, errorSize, "out of memory");
        return NULL;
    }
    if (ParseValue(&p, root, 0))
    {
        SkipSpace(&p);
        if (p.pos == p.length)
            return root;
        Fail(&p, "trailing data");
    }
    HostJson_Free(root);
    return NULL;
}

void HostJson_Free(struct HostJsonValue *value)
{
    if (value == NULL)
        return;
    FreeContents(value);
    free(value);
}

const struct HostJsonValue *HostJson_Get(const struct HostJsonValue *object, const char *key)
{
    size_t i, n = strlen(key);

    if (object == NULL || object->type != HOST_JSON_OBJECT)
        return NULL;
    for (i = 0; i < object->count; i++)
        if (object->items[i].keyLength == n && memcmp(object->items[i].key, key, n) == 0)
            return &object->items[i];
    return NULL;
}

bool HostJson_GetUint(const struct HostJsonValue *value, uint32_t max, uint32_t *out)
{
    if (value == NULL || value->type != HOST_JSON_NUMBER || value->number < 0
     || value->number > (double)max || value->number != (double)(uint32_t)value->number)
        return false;
    *out = (uint32_t)value->number;
    return true;
}

bool HostJson_GetInt(const struct HostJsonValue *value, int64_t min, int64_t max, int64_t *out)
{
    if (value == NULL || value->type != HOST_JSON_NUMBER || value->number < (double)min
     || value->number > (double)max || value->number != (double)(int64_t)value->number)
        return false;
    *out = (int64_t)value->number;
    return true;
}

/* --------------------------------------------------------------------- */
/* Writing                                                               */
/* --------------------------------------------------------------------- */

static void Append(struct HostJsonWriter *w, const char *s, size_t n)
{
    if (w->failed)
        return;
    if (w->length + n + 1 > w->capacity)
    {
        size_t capacity = w->capacity != 0 ? w->capacity : 4096;
        char *data;
        while (w->length + n + 1 > capacity)
            capacity *= 2;
        data = realloc(w->data, capacity);
        if (data == NULL)
        {
            w->failed = true;
            return;
        }
        w->data = data;
        w->capacity = capacity;
    }
    memcpy(w->data + w->length, s, n);
    w->length += n;
    w->data[w->length] = '\0';
}

static void AppendF(struct HostJsonWriter *w, const char *format, ...)
{
    char buffer[64];
    va_list args;
    int n;

    va_start(args, format);
    n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(buffer))
        w->failed = true;
    else
        Append(w, buffer, (size_t)n);
}

static void Newline(struct HostJsonWriter *w)
{
    int i;

    Append(w, "\n", 1);
    for (i = 0; i < w->depth; i++)
        Append(w, "  ", 2);
}

/* Separator and indentation before a value or key. */
static void BeforeItem(struct HostJsonWriter *w)
{
    if (w->afterKey)
    {
        w->afterKey = false;
        return;
    }
    if (w->depth > 0)
    {
        if (!w->first[w->depth])
            Append(w, w->oneLine[w->depth] ? ", " : ",", w->oneLine[w->depth] ? 2 : 1);
        w->first[w->depth] = false;
        if (!w->oneLine[w->depth])
            Newline(w);
    }
}

void HostJsonW_Init(struct HostJsonWriter *w)
{
    memset(w, 0, sizeof(*w));
}

void HostJsonW_Free(struct HostJsonWriter *w)
{
    free(w->data);
    memset(w, 0, sizeof(*w));
}

static void Begin(struct HostJsonWriter *w, char open, bool oneLine)
{
    BeforeItem(w);
    Append(w, &open, 1);
    if (w->depth + 1 >= (int)(sizeof(w->first) / sizeof(w->first[0])))
    {
        w->failed = true;
        return;
    }
    w->depth++;
    w->first[w->depth] = true;
    w->oneLine[w->depth] = oneLine || w->oneLine[w->depth - 1];
}

static void End(struct HostJsonWriter *w, char close)
{
    bool empty, oneLine;

    if (w->depth == 0)
    {
        w->failed = true;
        return;
    }
    empty = w->first[w->depth];
    oneLine = w->oneLine[w->depth];
    w->depth--;
    if (!empty && !oneLine)
        Newline(w);
    Append(w, &close, 1);
}

void HostJsonW_BeginObject(struct HostJsonWriter *w) { Begin(w, '{', false); }
void HostJsonW_EndObject(struct HostJsonWriter *w) { End(w, '}'); }
void HostJsonW_BeginArray(struct HostJsonWriter *w) { Begin(w, '[', false); }
void HostJsonW_BeginOneLineArray(struct HostJsonWriter *w) { Begin(w, '[', true); }
void HostJsonW_EndArray(struct HostJsonWriter *w) { End(w, ']'); }

static void WriteString(struct HostJsonWriter *w, const char *s, size_t length)
{
    size_t i, run = 0;

    Append(w, "\"", 1);
    for (i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\')
            continue;
        Append(w, s + run, i - run);
        run = i + 1;
        if (c == '"' || c == '\\')
        {
            char escaped[2] = { '\\', (char)c };
            Append(w, escaped, 2);
        }
        else
        {
            AppendF(w, "\\u%04x", c);
        }
    }
    Append(w, s + run, length - run);
    Append(w, "\"", 1);
}

void HostJsonW_Key(struct HostJsonWriter *w, const char *key)
{
    BeforeItem(w);
    WriteString(w, key, strlen(key));
    Append(w, ": ", 2);
    w->afterKey = true;
}

void HostJsonW_String(struct HostJsonWriter *w, const char *s, size_t length)
{
    BeforeItem(w);
    WriteString(w, s, length);
}

void HostJsonW_CString(struct HostJsonWriter *w, const char *s)
{
    HostJsonW_String(w, s, strlen(s));
}

void HostJsonW_Uint(struct HostJsonWriter *w, uint32_t value)
{
    BeforeItem(w);
    AppendF(w, "%lu", (unsigned long)value);
}

void HostJsonW_Int(struct HostJsonWriter *w, int32_t value)
{
    BeforeItem(w);
    AppendF(w, "%ld", (long)value);
}

void HostJsonW_Bool(struct HostJsonWriter *w, bool value)
{
    BeforeItem(w);
    Append(w, value ? "true" : "false", value ? 4 : 5);
}

bool HostJsonW_Finish(struct HostJsonWriter *w)
{
    if (w->depth != 0 || w->afterKey)
        w->failed = true;
    Append(w, "\n", 1);
    return !w->failed;
}
