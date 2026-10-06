/*
 * platform/include/platform/host_json.h
 *
 * A small JSON reader and writer for the save file (host_save_json.c).
 * Reading builds a tree; writing streams pretty-printed text into a growing
 * buffer. Strings are UTF-8 and may hold NUL bytes (length is explicit).
 */

#ifndef PLATFORM_HOST_JSON_H
#define PLATFORM_HOST_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum HostJsonType
{
    HOST_JSON_NULL,
    HOST_JSON_BOOL,
    HOST_JSON_NUMBER,
    HOST_JSON_STRING,
    HOST_JSON_ARRAY,
    HOST_JSON_OBJECT,
};

struct HostJsonValue
{
    enum HostJsonType type;
    char *key;                 /* member name, inside an object */
    size_t keyLength;
    bool boolean;
    double number;
    char *string;              /* NUL-terminated as well */
    size_t length;
    struct HostJsonValue *items;  /* array elements or object members */
    size_t count;
};

/* Parse `length` bytes of JSON. Returns NULL and a message (with the byte
 * offset) on malformed input; free the result with HostJson_Free. */
struct HostJsonValue *HostJson_Parse(const char *text, size_t length, char *error, size_t errorSize);
void HostJson_Free(struct HostJsonValue *value);

/* The member `key` of an object, or NULL (also when `object` isn't one). */
const struct HostJsonValue *HostJson_Get(const struct HostJsonValue *object, const char *key);

/* A whole number in [0, max]; false otherwise (wrong type, fraction, range). */
bool HostJson_GetUint(const struct HostJsonValue *value, uint32_t max, uint32_t *out);

/* A whole number in [min, max]; false otherwise. */
bool HostJson_GetInt(const struct HostJsonValue *value, int64_t min, int64_t max, int64_t *out);

struct HostJsonWriter
{
    char *data;
    size_t length;
    size_t capacity;
    bool failed;               /* out of memory or misuse; data is unusable */
    int depth;
    bool first[32];            /* nothing written yet at this depth */
    bool oneLine[32];          /* this depth is written on one line */
    bool afterKey;
};

void HostJsonW_Init(struct HostJsonWriter *w);
void HostJsonW_Free(struct HostJsonWriter *w);
void HostJsonW_BeginObject(struct HostJsonWriter *w);
void HostJsonW_EndObject(struct HostJsonWriter *w);
void HostJsonW_BeginArray(struct HostJsonWriter *w);
/* An array written on one line, with everything inside it. */
void HostJsonW_BeginOneLineArray(struct HostJsonWriter *w);
void HostJsonW_EndArray(struct HostJsonWriter *w);
void HostJsonW_Key(struct HostJsonWriter *w, const char *key);
void HostJsonW_String(struct HostJsonWriter *w, const char *s, size_t length);
void HostJsonW_CString(struct HostJsonWriter *w, const char *s);
void HostJsonW_Uint(struct HostJsonWriter *w, uint32_t value);
void HostJsonW_Int(struct HostJsonWriter *w, int32_t value);
void HostJsonW_Bool(struct HostJsonWriter *w, bool value);
/* Ends the document with a newline; true if the text is complete. */
bool HostJsonW_Finish(struct HostJsonWriter *w);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_JSON_H */
