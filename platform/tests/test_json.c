/*
 * platform/tests/test_json.c
 *
 * The JSON reader and writer (host_json.c): valid documents, escapes and
 * numbers, malformed input refused, and writer output that reads back.
 */

#include <stdio.h>
#include <string.h>

#include "platform/host_json.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static struct HostJsonValue *Parse(const char *text)
{
    char error[128];
    return HostJson_Parse(text, strlen(text), error, sizeof(error));
}

static void CheckRejected(const char *text, const char *what)
{
    struct HostJsonValue *v = Parse(text);
    Check(v == NULL, what);
    HostJson_Free(v);
}

int main(void)
{
    static const char *const malformed[][2] = {
        { "", "empty input" },
        { "{", "unterminated object" },
        { "[1,]", "trailing comma" },
        { "{\"a\" 1}", "missing colon" },
        { "{a: 1}", "unquoted key" },
        { "\"abc", "unterminated string" },
        { "\"a\nb\"", "raw newline in a string" },
        { "\"\\x41\"", "invalid escape" },
        { "\"\\ud800\"", "lone high surrogate" },
        { "\"\\udc00\"", "lone low surrogate" },
        { "01", "leading zero" },
        { "1.", "fraction without digits" },
        { "-", "lone minus" },
        { "tru", "truncated literal" },
        { "{} {}", "trailing data" },
    };
    struct HostJsonValue *v;
    struct HostJsonWriter w;
    char deep[200];
    uint32_t n;
    size_t i;

    v = Parse(" {\"a\": [1, -2.5e1, true, false, null], \"b\": {\"c\": \"x\\\"\\\\\\/\\n\\u00e9\\ud83d\\ude00\"}} ");
    Check(v != NULL && v->type == HOST_JSON_OBJECT && v->count == 2, "parse an object");
    if (v != NULL)
    {
        const struct HostJsonValue *a = HostJson_Get(v, "a");
        const struct HostJsonValue *c = HostJson_Get(HostJson_Get(v, "b"), "c");
        Check(a != NULL && a->type == HOST_JSON_ARRAY && a->count == 5, "array members");
        Check(a != NULL && a->items[1].number == -25.0, "a number with fraction and exponent");
        Check(a != NULL && a->items[2].boolean && !a->items[3].boolean && a->items[4].type == HOST_JSON_NULL, "literals");
        Check(c != NULL && c->length == 11 && memcmp(c->string, "x\"\\/\n\xc3\xa9\xf0\x9f\x98\x80", 11) == 0,
              "string escapes, \\u and surrogate pairs");
        Check(HostJson_Get(v, "missing") == NULL && HostJson_Get(a, "a") == NULL, "missing members");
        Check(HostJson_GetUint(&a->items[0], 10, &n) && n == 1, "a whole number");
        Check(!HostJson_GetUint(&a->items[1], 100, &n), "a negative number isn't a uint");
    }
    HostJson_Free(v);

    v = Parse("[4294967295, 4294967296, 1.5, \"1\"]");
    Check(v != NULL && HostJson_GetUint(&v->items[0], 0xFFFFFFFF, &n) && n == 0xFFFFFFFF, "the largest u32");
    Check(v != NULL && !HostJson_GetUint(&v->items[1], 0xFFFFFFFF, &n), "past u32 is refused");
    Check(v != NULL && !HostJson_GetUint(&v->items[2], 10, &n), "a fraction is refused");
    Check(v != NULL && !HostJson_GetUint(&v->items[3], 10, &n), "a string is refused");
    HostJson_Free(v);

    v = Parse("\"a\\u0000b\"");
    Check(v != NULL && v->length == 3 && v->string[1] == '\0', "NUL inside a string");
    HostJson_Free(v);

    for (i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
        CheckRejected(malformed[i][0], malformed[i][1]);
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = '\0';
    CheckRejected(deep, "nesting past the depth limit");

    HostJsonW_Init(&w);
    HostJsonW_BeginObject(&w);
    HostJsonW_Key(&w, "s");
    HostJsonW_String(&w, "q\"\\\n\x01", 5);
    HostJsonW_Key(&w, "list");
    HostJsonW_BeginArray(&w);
    HostJsonW_Uint(&w, 4294967295u);
    HostJsonW_Int(&w, -7);
    HostJsonW_Bool(&w, true);
    HostJsonW_EndArray(&w);
    HostJsonW_Key(&w, "empty");
    HostJsonW_BeginObject(&w);
    HostJsonW_EndObject(&w);
    HostJsonW_EndObject(&w);
    Check(HostJsonW_Finish(&w), "write a document");
    v = w.data != NULL ? Parse(w.data) : NULL;
    Check(v != NULL, "written JSON parses");
    if (v != NULL)
    {
        const struct HostJsonValue *s = HostJson_Get(v, "s");
        const struct HostJsonValue *list = HostJson_Get(v, "list");
        Check(s != NULL && s->length == 5 && memcmp(s->string, "q\"\\\n\x01", 5) == 0, "strings round-trip");
        Check(list != NULL && list->count == 3 && HostJson_GetUint(&list->items[0], 0xFFFFFFFF, &n)
              && n == 4294967295u && list->items[1].number == -7, "numbers round-trip");
        Check(HostJson_Get(v, "empty") != NULL && HostJson_Get(v, "empty")->count == 0, "an empty object");
    }
    HostJson_Free(v);
    HostJsonW_Free(&w);

    HostJsonW_Init(&w);
    HostJsonW_BeginArray(&w);
    Check(!HostJsonW_Finish(&w), "an unfinished document is an error");
    HostJsonW_Free(&w);

    printf(sFailures ? "json: %d FAILED\n" : "json: all tests passed\n", sFailures);
    return sFailures != 0;
}
