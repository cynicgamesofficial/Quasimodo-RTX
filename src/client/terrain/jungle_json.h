/*
 * Quasimodo RTX — minimal UTF-8 JSON DOM helper for .jungle files.
 * Parser implementation lives in terrain_jungle.cpp (single TU).
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JJSON_NULL,
    JJSON_BOOL,
    JJSON_NUMBER,
    JJSON_STRING,
    JJSON_ARRAY,
    JJSON_OBJECT
} jjson_type_t;

typedef struct jjson_value jjson_value_t;

bool jjson_parse(const char *utf8, size_t len, jjson_value_t **out_root,
                 char *errbuf, size_t errbuf_sz);
void jjson_free(jjson_value_t *v);

const jjson_value_t *jjson_object_get(const jjson_value_t *obj, const char *key);
size_t jjson_object_count(const jjson_value_t *obj);
bool jjson_object_key_at(const jjson_value_t *obj, size_t index,
                         const char **key_out, const jjson_value_t **val_out);

size_t jjson_array_length(const jjson_value_t *arr);
const jjson_value_t *jjson_array_get(const jjson_value_t *arr, size_t index);

jjson_type_t jjson_type(const jjson_value_t *v);
bool jjson_is_null(const jjson_value_t *v);
bool jjson_bool(const jjson_value_t *v, bool *out);
bool jjson_number(const jjson_value_t *v, double *out);
bool jjson_string(const jjson_value_t *v, const char **out, size_t *len_out);

#ifdef __cplusplus
}
#endif
