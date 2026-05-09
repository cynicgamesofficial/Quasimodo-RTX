/*
 * Quasimodo RTX — .jungle JSON parse + CPU asset orchestration (Phase 2).
 */

#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <stdint.h>

#include "jungle_json.h"
#include "terrain.h"
#include "terrain_internal.h"

extern "C" {
#include "common/common.h"
#include "common/files.h"
#include "common/zone.h"
}

extern bool TerrainAsset_LoadHeightmapCPU(const char *vfs_path,
                                          int width, int height,
                                          const char *height_format,
                                          uint8_t **out_buf, size_t *out_bytes,
                                          char *errbuf, size_t errbuf_sz);

extern bool TerrainAsset_LoadTextureCPU8(const char *vfs_path,
                                         uint8_t **out_buf, size_t *out_bytes,
                                         int *out_w, int *out_h, int *out_comp,
                                         char *errbuf, size_t errbuf_sz);

extern void TerrainAsset_FreeBuffer(void *p);

extern bool TerrainSeam_ValidatePatchList(const char *const *paths, int count,
                                          char *errbuf, size_t errbuf_sz);

#define JJSON_MAX_DEPTH 48

struct jjson_value {
    jjson_type_t type;
    union {
        double number;
        bool boolean;
        struct {
            char *data;
            size_t len;
        } str;
        struct {
            jjson_value_t **items;
            size_t count;
        } arr;
        struct {
            char **keys;
            jjson_value_t **vals;
            size_t count;
        } obj;
    } u;
};

typedef struct {
    const char *p;
    const char *end;
    char *err;
    size_t err_sz;
    int depth;
} jjson_parse_ctx_t;

static void jjson_free_value(jjson_value_t *v);

static void jjson_set_err(jjson_parse_ctx_t *c, const char *msg)
{
    if (c->err && c->err_sz)
        Q_snprintf(c->err, c->err_sz, "%s", msg);
}

static void skip_ws(jjson_parse_ctx_t *c)
{
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            c->p++;
            continue;
        }
        break;
    }
}

static jjson_value_t *jjson_new(jjson_type_t t)
{
    jjson_value_t *v = (jjson_value_t *)Z_TagMallocz(sizeof(jjson_value_t), TAG_RENDERER);
    v->type = t;
    return v;
}

static bool hex4(const char *p, uint16_t *out)
{
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        char d = p[i];
        int x = -1;
        if (d >= '0' && d <= '9')
            x = d - '0';
        else if (d >= 'a' && d <= 'f')
            x = 10 + (d - 'a');
        else if (d >= 'A' && d <= 'F')
            x = 10 + (d - 'A');
        if (x < 0)
            return false;
        v = (uint16_t)((v << 4) | (unsigned)x);
    }
    *out = v;
    return true;
}

static bool utf8_append(uint32_t cp, char *dst, size_t dst_sz, size_t *used)
{
    if (cp <= 0x7f) {
        if (*used + 1 >= dst_sz)
            return false;
        dst[(*used)++] = (char)cp;
        return true;
    }
    if (cp <= 0x7ff) {
        if (*used + 2 >= dst_sz)
            return false;
        dst[(*used)++] = (char)(0xc0 | ((cp >> 6) & 0x1f));
        dst[(*used)++] = (char)(0x80 | (cp & 0x3f));
        return true;
    }
    if (cp <= 0xffff) {
        if (*used + 3 >= dst_sz)
            return false;
        dst[(*used)++] = (char)(0xe0 | ((cp >> 12) & 0x0f));
        dst[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[(*used)++] = (char)(0x80 | (cp & 0x3f));
        return true;
    }
    return false;
}

static bool parse_string(jjson_parse_ctx_t *c, char **out_s, size_t *out_len)
{
    if (c->p >= c->end || *c->p != '"') {
        jjson_set_err(c, "expected '\"'");
        return false;
    }
    c->p++;

    char tmp[4096];
    size_t u = 0;

    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p;
        if (ch == '"') {
            c->p++;
            *out_s = (char *)Z_TagMalloc(u + 1, TAG_RENDERER);
            memcpy(*out_s, tmp, u);
            (*out_s)[u] = '\0';
            *out_len = u;
            return true;
        }
        if (ch == '\\') {
            c->p++;
            if (c->p >= c->end) {
                jjson_set_err(c, "string escape: eof");
                return false;
            }
            char e = *c->p++;
            switch (e) {
            case '"':
                ch = '"';
                break;
            case '\\':
                ch = '\\';
                break;
            case '/':
                ch = '/';
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'u': {
                if (c->p + 4 > c->end) {
                    jjson_set_err(c, "\\u: eof");
                    return false;
                }
                uint16_t cp = 0;
                if (!hex4(c->p, &cp)) {
                    jjson_set_err(c, "\\u: bad hex");
                    return false;
                }
                c->p += 4;
                if (!utf8_append(cp, tmp, sizeof(tmp), &u)) {
                    jjson_set_err(c, "string overflow");
                    return false;
                }
                continue;
            }
            default:
                jjson_set_err(c, "bad escape");
                return false;
            }
        } else if ((unsigned char)ch < 0x20) {
            jjson_set_err(c, "control char in string");
            return false;
        } else {
            c->p++;
        }

        if (u + 1 >= sizeof(tmp)) {
            jjson_set_err(c, "string too long");
            return false;
        }
        tmp[u++] = (char)ch;
    }

    jjson_set_err(c, "unterminated string");
    return false;
}

static bool parse_number(jjson_parse_ctx_t *c, double *out)
{
    const char *start = c->p;
    if (c->p < c->end && (*c->p == '-' || *c->p == '+'))
        c->p++;
    while (c->p < c->end && isdigit((unsigned char)*c->p))
        c->p++;
    if (c->p < c->end && *c->p == '.') {
        c->p++;
        while (c->p < c->end && isdigit((unsigned char)*c->p))
            c->p++;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        c->p++;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-'))
            c->p++;
        while (c->p < c->end && isdigit((unsigned char)*c->p))
            c->p++;
    }

    size_t nlen = (size_t)(c->p - start);
    if (nlen == 0) {
        jjson_set_err(c, "bad number");
        return false;
    }

    char nb[128];
    if (nlen >= sizeof(nb)) {
        jjson_set_err(c, "number token too long");
        return false;
    }
    memcpy(nb, start, nlen);
    nb[nlen] = '\0';

    char *endp = nullptr;
    double v = strtod(nb, &endp);
    if (!endp || *endp != '\0') {
        jjson_set_err(c, "number parse failed");
        return false;
    }
    *out = v;
    return true;
}

static jjson_value_t *parse_value(jjson_parse_ctx_t *c);

static jjson_value_t *parse_array(jjson_parse_ctx_t *c)
{
    if (c->depth >= JJSON_MAX_DEPTH) {
        jjson_set_err(c, "nesting too deep");
        return nullptr;
    }
    c->depth++;
    jjson_value_t *v = jjson_new(JJSON_ARRAY);
    skip_ws(c);
    if (c->p < c->end && *c->p == ']') {
        c->p++;
        c->depth--;
        return v;
    }

    for (;;) {
        jjson_value_t *elem = parse_value(c);
        if (!elem) {
            jjson_free_value(v);
            return nullptr;
        }

        size_t n = v->u.arr.count + 1;
        v->u.arr.items = (jjson_value_t **)Z_Realloc(v->u.arr.items, n * sizeof(jjson_value_t *));
        v->u.arr.items[v->u.arr.count++] = elem;

        skip_ws(c);
        if (c->p >= c->end) {
            jjson_set_err(c, "array: eof");
            jjson_free_value(v);
            return nullptr;
        }
        if (*c->p == ']') {
            c->p++;
            break;
        }
        if (*c->p != ',') {
            jjson_set_err(c, "array: expected ','");
            jjson_free_value(v);
            return nullptr;
        }
        c->p++;
        skip_ws(c);
    }

    c->depth--;
    return v;
}

static jjson_value_t *parse_object(jjson_parse_ctx_t *c)
{
    if (c->depth >= JJSON_MAX_DEPTH) {
        jjson_set_err(c, "nesting too deep");
        return nullptr;
    }
    c->depth++;
    jjson_value_t *v = jjson_new(JJSON_OBJECT);
    skip_ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        c->depth--;
        return v;
    }

    for (;;) {
        skip_ws(c);
        char *ks = nullptr;
        size_t kl = 0;
        if (!parse_string(c, &ks, &kl)) {
            jjson_free_value(v);
            Z_Free(ks);
            return nullptr;
        }

        skip_ws(c);
        if (c->p >= c->end || *c->p != ':') {
            jjson_set_err(c, "object: expected ':'");
            Z_Free(ks);
            jjson_free_value(v);
            return nullptr;
        }
        c->p++;

        jjson_value_t *ev = parse_value(c);
        if (!ev) {
            Z_Free(ks);
            jjson_free_value(v);
            return nullptr;
        }

        size_t n = v->u.obj.count + 1;
        v->u.obj.keys = (char **)Z_Realloc(v->u.obj.keys, n * sizeof(char *));
        v->u.obj.vals = (jjson_value_t **)Z_Realloc(v->u.obj.vals, n * sizeof(jjson_value_t *));
        v->u.obj.keys[v->u.obj.count] = ks;
        v->u.obj.vals[v->u.obj.count] = ev;
        v->u.obj.count++;

        skip_ws(c);
        if (c->p >= c->end) {
            jjson_set_err(c, "object: eof");
            jjson_free_value(v);
            return nullptr;
        }
        if (*c->p == '}') {
            c->p++;
            break;
        }
        if (*c->p != ',') {
            jjson_set_err(c, "object: expected ','");
            jjson_free_value(v);
            return nullptr;
        }
        c->p++;
    }

    c->depth--;
    return v;
}

static jjson_value_t *parse_value(jjson_parse_ctx_t *c)
{
    skip_ws(c);
    if (c->p >= c->end) {
        jjson_set_err(c, "unexpected eof");
        return nullptr;
    }

    char ch = *c->p;
    if (ch == '{')
        return (c->p++, parse_object(c));
    if (ch == '[')
        return (c->p++, parse_array(c));
    if (ch == '"') {
        char *s = nullptr;
        size_t sl = 0;
        if (!parse_string(c, &s, &sl))
            return nullptr;
        jjson_value_t *v = jjson_new(JJSON_STRING);
        v->u.str.data = s;
        v->u.str.len = sl;
        return v;
    }
    if (ch == '-' || ch == '+' || isdigit((unsigned char)ch)) {
        double num = 0;
        if (!parse_number(c, &num))
            return nullptr;
        jjson_value_t *v = jjson_new(JJSON_NUMBER);
        v->u.number = num;
        return v;
    }
    if (c->end - c->p >= 4 && !strncmp(c->p, "true", 4)) {
        c->p += 4;
        jjson_value_t *v = jjson_new(JJSON_BOOL);
        v->u.boolean = true;
        return v;
    }
    if (c->end - c->p >= 5 && !strncmp(c->p, "false", 5)) {
        c->p += 5;
        jjson_value_t *v = jjson_new(JJSON_BOOL);
        v->u.boolean = false;
        return v;
    }
    if (c->end - c->p >= 4 && !strncmp(c->p, "null", 4)) {
        c->p += 4;
        return jjson_new(JJSON_NULL);
    }

    jjson_set_err(c, "unexpected token");
    return nullptr;
}

void jjson_free(jjson_value_t *v)
{
    jjson_free_value(v);
}

static void jjson_free_value(jjson_value_t *v)
{
    if (!v)
        return;
    switch (v->type) {
    case JJSON_STRING:
        Z_Free(v->u.str.data);
        break;
    case JJSON_ARRAY:
        for (size_t i = 0; i < v->u.arr.count; i++)
            jjson_free_value(v->u.arr.items[i]);
        Z_Free(v->u.arr.items);
        break;
    case JJSON_OBJECT:
        for (size_t i = 0; i < v->u.obj.count; i++) {
            Z_Free(v->u.obj.keys[i]);
            jjson_free_value(v->u.obj.vals[i]);
        }
        Z_Free(v->u.obj.keys);
        Z_Free(v->u.obj.vals);
        break;
    default:
        break;
    }
    Z_Free(v);
}

bool jjson_parse(const char *utf8, size_t len, jjson_value_t **out_root,
                 char *errbuf, size_t errbuf_sz)
{
    jjson_parse_ctx_t ctx;
    ctx.p = utf8;
    ctx.end = utf8 + len;
    ctx.err = errbuf;
    ctx.err_sz = errbuf_sz;
    ctx.depth = 0;

    if (errbuf && errbuf_sz)
        errbuf[0] = '\0';

    /* UTF-8 BOM */
    if (len >= 3 && (unsigned char)utf8[0] == 0xef && (unsigned char)utf8[1] == 0xbb &&
        (unsigned char)utf8[2] == 0xbf) {
        ctx.p += 3;
    }

    skip_ws(&ctx);
    jjson_value_t *root = parse_value(&ctx);
    if (!root)
        return false;
    skip_ws(&ctx);
    if (ctx.p < ctx.end) {
        jjson_set_err(&ctx, "trailing garbage");
        jjson_free_value(root);
        return false;
    }
    *out_root = root;
    return true;
}

const jjson_value_t *jjson_object_get(const jjson_value_t *obj, const char *key)
{
    if (!obj || obj->type != JJSON_OBJECT || !key)
        return nullptr;
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        if (!strcmp(obj->u.obj.keys[i], key))
            return obj->u.obj.vals[i];
    }
    return nullptr;
}

size_t jjson_object_count(const jjson_value_t *obj)
{
    if (!obj || obj->type != JJSON_OBJECT)
        return 0;
    return obj->u.obj.count;
}

bool jjson_object_key_at(const jjson_value_t *obj, size_t index,
                         const char **key_out, const jjson_value_t **val_out)
{
    if (!obj || obj->type != JJSON_OBJECT)
        return false;
    if (index >= obj->u.obj.count)
        return false;
    if (key_out)
        *key_out = obj->u.obj.keys[index];
    if (val_out)
        *val_out = obj->u.obj.vals[index];
    return true;
}

size_t jjson_array_length(const jjson_value_t *arr)
{
    if (!arr || arr->type != JJSON_ARRAY)
        return 0;
    return arr->u.arr.count;
}

const jjson_value_t *jjson_array_get(const jjson_value_t *arr, size_t index)
{
    if (!arr || arr->type != JJSON_ARRAY || index >= arr->u.arr.count)
        return nullptr;
    return arr->u.arr.items[index];
}

jjson_type_t jjson_type(const jjson_value_t *v)
{
    return v ? v->type : JJSON_NULL;
}

bool jjson_is_null(const jjson_value_t *v)
{
    return !v || v->type == JJSON_NULL;
}

bool jjson_bool(const jjson_value_t *v, bool *out)
{
    if (!v || v->type != JJSON_BOOL || !out)
        return false;
    *out = v->u.boolean;
    return true;
}

bool jjson_number(const jjson_value_t *v, double *out)
{
    if (!v || v->type != JJSON_NUMBER || !out)
        return false;
    *out = v->u.number;
    return true;
}

bool jjson_string(const jjson_value_t *v, const char **out, size_t *len_out)
{
    if (!v || v->type != JJSON_STRING || !out)
        return false;
    *out = v->u.str.data;
    if (len_out)
        *len_out = v->u.str.len;
    return true;
}

/* ------------------------------------------------------------------------- */

static void jungle_doc_clear_buffers(jungle_document_t *d)
{
    if (!d)
        return;
    TerrainAsset_FreeBuffer(d->cpu_heightmap);
    d->cpu_heightmap = nullptr;
    d->cpu_heightmap_bytes = 0;
    TerrainAsset_FreeBuffer(d->cpu_splat);
    d->cpu_splat = nullptr;
    d->cpu_splat_bytes = 0;
    TerrainAsset_FreeBuffer(d->cpu_water_mask);
    d->cpu_water_mask = nullptr;
    d->cpu_water_mask_bytes = 0;
}

static void jungle_doc_free_strings(jungle_document_t *d)
{
    if (!d)
        return;
    for (int i = 0; i < d->terrain_materials_count; i++)
        Z_Free(d->terrain_materials[i]);
    Z_Free(d->terrain_materials);
    d->terrain_materials = nullptr;
    d->terrain_materials_count = 0;

    for (int i = 0; i < d->seam_patch_paths_count; i++)
        Z_Free(d->seam_patch_paths[i]);
    Z_Free(d->seam_patch_paths);
    d->seam_patch_paths = nullptr;
    d->seam_patch_paths_count = 0;
}

void TerrainJungle_FreeDocument(void *opaque)
{
    jungle_document_t *d = (jungle_document_t *)opaque;
    if (!d)
        return;
    jungle_doc_clear_buffers(d);
    jungle_doc_free_strings(d);
    Z_Free(d);
}

static bool copy_qpath(char *dst, const char *src)
{
    if (!dst)
        return false;
    dst[0] = '\0';
    if (!src)
        return true;
    Q_strlcpy(dst, src, MAX_QPATH);
    return true;
}

static void warn_unknown_root_key(const char *k)
{
    Com_WPrintf("[TERRAIN] unknown root field \"%s\" (ignored)\n", k);
}

static bool parse_mode_string(const char *s, jungle_mode_enum_t *out)
{
    if (!s || !out)
        return false;
    if (!strcmp(s, "terrain_only")) {
        *out = JUNGLE_MODE_TERRAIN_ONLY;
        return true;
    }
    if (!strcmp(s, "bsp_terrain")) {
        *out = JUNGLE_MODE_BSP_TERRAIN;
        return true;
    }
    if (!strcmp(s, "bsp_only")) {
        *out = JUNGLE_MODE_BSP_ONLY;
        return true;
    }
    return false;
}

static bool jungle_fill_from_json(jungle_document_t *doc, const jjson_value_t *root,
                                  char *errbuf, size_t errbuf_sz)
{
    if (!root || root->type != JJSON_OBJECT) {
        Q_snprintf(errbuf, errbuf_sz, "root must be object");
        return false;
    }

    /* Unknown root keys */
    for (size_t i = 0; i < jjson_object_count(root); i++) {
        const char *k = nullptr;
        const jjson_value_t *vv = nullptr;
        jjson_object_key_at(root, i, &k, &vv);
        if (!k)
            continue;
        if (strcmp(k, "version") && strcmp(k, "format") && strcmp(k, "mode") &&
            strcmp(k, "world") && strcmp(k, "terrain") && strcmp(k, "water") &&
            strcmp(k, "sky") && strcmp(k, "collision") && strcmp(k, "debug")) {
            warn_unknown_root_key(k);
        }
    }

    const jjson_value_t *jv = jjson_object_get(root, "version");
    double vn = 0;
    if (!jv || !jjson_number(jv, &vn)) {
        Q_snprintf(errbuf, errbuf_sz, "missing or invalid \"version\"");
        return false;
    }
    doc->version = (int)vn;

    jv = jjson_object_get(root, "format");
    const char *fs = nullptr;
    size_t fslen = 0;
    if (!jv || !jjson_string(jv, &fs, &fslen)) {
        Q_snprintf(errbuf, errbuf_sz, "missing or invalid \"format\"");
        return false;
    }
    if (strcmp(fs, "jungle")) {
        Q_snprintf(errbuf, errbuf_sz, "format must be \"jungle\"");
        return false;
    }
    Q_strlcpy(doc->format_str, "jungle", sizeof(doc->format_str));

    jv = jjson_object_get(root, "mode");
    if (!jv || !jjson_string(jv, &fs, &fslen)) {
        Q_snprintf(errbuf, errbuf_sz, "missing or invalid \"mode\"");
        return false;
    }
    char modebuf[64];
    if (fslen >= sizeof(modebuf)) {
        Q_snprintf(errbuf, errbuf_sz, "mode string too long");
        return false;
    }
    memcpy(modebuf, fs, fslen);
    modebuf[fslen] = '\0';
    if (!parse_mode_string(modebuf, &doc->mode)) {
        Q_snprintf(errbuf, errbuf_sz, "unknown mode \"%s\"", modebuf);
        return false;
    }

    /* defaults */
    doc->world_bsp[0] = '\0';
    doc->world_bsp_interior_only = false;
    doc->terrain_heightmap[0] = '\0';
    doc->terrain_width = 0;
    doc->terrain_height = 0;
    Q_strlcpy(doc->terrain_height_format, "uint16_le", sizeof(doc->terrain_height_format));
    doc->terrain_scale_xy = 1.f;
    doc->terrain_scale_z = 1.f;
    VectorClear(doc->terrain_origin);
    doc->terrain_chunk_size = 256;
    doc->terrain_lod_count = 1;
    doc->terrain_lod_morph = false;
    doc->terrain_splatmap[0] = '\0';
    doc->water_enabled = false;
    doc->water_level = 0.f;
    doc->water_mask[0] = '\0';
    doc->water_normal_map[0] = '\0';
    doc->water_normal_scroll[0] = doc->water_normal_scroll[1] = 0.f;
    doc->water_color[0] = doc->water_color[1] = doc->water_color[2] = 1.f;
    doc->water_color[3] = 1.f;
    doc->water_roughness = 0.1f;
    doc->water_refraction_strength = 0.f;
    doc->water_reflection_mode[0] = '\0';
    doc->water_rtx_reflection_hook = false;
    doc->sky_override = false;
    doc->sky_shader[0] = '\0';
    doc->collision_terrain = true;
    doc->collision_water = true;
    doc->collision_seam = true;
    doc->debug_draw_chunks = false;
    doc->debug_draw_lod = false;
    doc->debug_draw_seams = false;
    doc->debug_draw_water_mask = false;

    const jjson_value_t *wobj = jjson_object_get(root, "world");
    if (wobj && wobj->type == JJSON_OBJECT) {
        const jjson_value_t *t = nullptr;
        t = jjson_object_get(wobj, "bsp");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->world_bsp, fs);
        t = jjson_object_get(wobj, "bsp_interior_only");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->world_bsp_interior_only = b;
        }
    }

    const jjson_value_t *tobj = jjson_object_get(root, "terrain");
    if (tobj && tobj->type == JJSON_OBJECT) {
        const jjson_value_t *t = jjson_object_get(tobj, "heightmap");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->terrain_heightmap, fs);

        t = jjson_object_get(tobj, "width");
        if (t && jjson_number(t, &vn))
            doc->terrain_width = (int)vn;
        t = jjson_object_get(tobj, "height");
        if (t && jjson_number(t, &vn))
            doc->terrain_height = (int)vn;

        t = jjson_object_get(tobj, "height_format");
        if (t && jjson_string(t, &fs, &fslen)) {
            char hf[32];
            size_t cpy = fslen < sizeof(hf) - 1 ? fslen : sizeof(hf) - 1;
            memcpy(hf, fs, cpy);
            hf[cpy] = '\0';
            Q_strlcpy(doc->terrain_height_format, hf, sizeof(doc->terrain_height_format));
        }

        t = jjson_object_get(tobj, "scale_xy");
        if (t && jjson_number(t, &vn))
            doc->terrain_scale_xy = (float)vn;
        t = jjson_object_get(tobj, "scale_z");
        if (t && jjson_number(t, &vn))
            doc->terrain_scale_z = (float)vn;

        const jjson_value_t *orig = jjson_object_get(tobj, "origin");
        if (orig && orig->type == JJSON_ARRAY && jjson_array_length(orig) >= 3) {
            double a, b, c;
            if (jjson_number(jjson_array_get(orig, 0), &a) &&
                jjson_number(jjson_array_get(orig, 1), &b) &&
                jjson_number(jjson_array_get(orig, 2), &c)) {
                doc->terrain_origin[0] = (float)a;
                doc->terrain_origin[1] = (float)b;
                doc->terrain_origin[2] = (float)c;
            }
        }

        t = jjson_object_get(tobj, "chunk_size");
        if (t && jjson_number(t, &vn))
            doc->terrain_chunk_size = (int)vn;
        t = jjson_object_get(tobj, "lod_count");
        if (t && jjson_number(t, &vn))
            doc->terrain_lod_count = (int)vn;
        t = jjson_object_get(tobj, "lod_morph");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->terrain_lod_morph = b;
        }

        t = jjson_object_get(tobj, "splatmap");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->terrain_splatmap, fs);

        const jjson_value_t *marr = jjson_object_get(tobj, "materials");
        if (marr && marr->type == JJSON_ARRAY) {
            size_t n = jjson_array_length(marr);
            doc->terrain_materials = (char **)Z_TagMallocz((size_t)n * sizeof(char *), TAG_RENDERER);
            doc->terrain_materials_count = (int)n;
            for (size_t i = 0; i < n; i++) {
                const jjson_value_t *it = jjson_array_get(marr, i);
                if (it && jjson_string(it, &fs, &fslen)) {
                    char *one = (char *)Z_TagMalloc(fslen + 1, TAG_RENDERER);
                    memcpy(one, fs, fslen);
                    one[fslen] = '\0';
                    doc->terrain_materials[i] = one;
                }
            }
        }

        const jjson_value_t *sarr = jjson_object_get(tobj, "seam_patches");
        if (sarr && sarr->type == JJSON_ARRAY) {
            size_t n = jjson_array_length(sarr);
            doc->seam_patch_paths = (char **)Z_TagMallocz((size_t)n * sizeof(char *), TAG_RENDERER);
            doc->seam_patch_paths_count = (int)n;
            for (size_t i = 0; i < n; i++) {
                const jjson_value_t *it = jjson_array_get(sarr, i);
                if (it && jjson_string(it, &fs, &fslen)) {
                    char *one = (char *)Z_TagMalloc(fslen + 1, TAG_RENDERER);
                    memcpy(one, fs, fslen);
                    one[fslen] = '\0';
                    doc->seam_patch_paths[i] = one;
                }
            }
        }
    }

    const jjson_value_t *wo = jjson_object_get(root, "water");
    if (wo && wo->type == JJSON_OBJECT) {
        const jjson_value_t *t = jjson_object_get(wo, "enabled");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->water_enabled = b;
        }
        t = jjson_object_get(wo, "level");
        if (t && jjson_number(t, &vn))
            doc->water_level = (float)vn;
        t = jjson_object_get(wo, "mask");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->water_mask, fs);
        t = jjson_object_get(wo, "normal_map");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->water_normal_map, fs);

        const jjson_value_t *ns = jjson_object_get(wo, "normal_scroll");
        if (ns && ns->type == JJSON_ARRAY && jjson_array_length(ns) >= 2) {
            double a, b;
            if (jjson_number(jjson_array_get(ns, 0), &a) &&
                jjson_number(jjson_array_get(ns, 1), &b)) {
                doc->water_normal_scroll[0] = (float)a;
                doc->water_normal_scroll[1] = (float)b;
            }
        }

        const jjson_value_t *col = jjson_object_get(wo, "color");
        if (col && col->type == JJSON_ARRAY) {
            size_t nc = jjson_array_length(col);
            for (size_t i = 0; i < nc && i < 4; i++) {
                double c = 0;
                if (jjson_number(jjson_array_get(col, i), &c))
                    doc->water_color[i] = (float)c;
            }
        }

        t = jjson_object_get(wo, "roughness");
        if (t && jjson_number(t, &vn))
            doc->water_roughness = (float)vn;
        t = jjson_object_get(wo, "refraction_strength");
        if (t && jjson_number(t, &vn))
            doc->water_refraction_strength = (float)vn;
        t = jjson_object_get(wo, "reflection_mode");
        if (t && jjson_string(t, &fs, &fslen)) {
            char rm[32];
            size_t cpy = fslen < sizeof(rm) - 1 ? fslen : sizeof(rm) - 1;
            memcpy(rm, fs, cpy);
            rm[cpy] = '\0';
            Q_strlcpy(doc->water_reflection_mode, rm, sizeof(doc->water_reflection_mode));
        }
        t = jjson_object_get(wo, "rtx_reflection_hook");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->water_rtx_reflection_hook = b;
        }
    }

    const jjson_value_t *sk = jjson_object_get(root, "sky");
    if (sk && sk->type == JJSON_OBJECT) {
        const jjson_value_t *t = jjson_object_get(sk, "override");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->sky_override = b;
        }
        t = jjson_object_get(sk, "sky_shader");
        if (t && jjson_string(t, &fs, &fslen))
            copy_qpath(doc->sky_shader, fs);
    }

    const jjson_value_t *co = jjson_object_get(root, "collision");
    if (co && co->type == JJSON_OBJECT) {
        const jjson_value_t *t = jjson_object_get(co, "terrain_collision");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->collision_terrain = b;
        }
        t = jjson_object_get(co, "water_collision");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->collision_water = b;
        }
        t = jjson_object_get(co, "seam_collision");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->collision_seam = b;
        }
    }

    const jjson_value_t *dbg = jjson_object_get(root, "debug");
    if (dbg && dbg->type == JJSON_OBJECT) {
        const jjson_value_t *t = jjson_object_get(dbg, "draw_chunks");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->debug_draw_chunks = b;
        }
        t = jjson_object_get(dbg, "draw_lod");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->debug_draw_lod = b;
        }
        t = jjson_object_get(dbg, "draw_seams");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->debug_draw_seams = b;
        }
        t = jjson_object_get(dbg, "draw_water_mask");
        if (t && t->type == JJSON_BOOL) {
            bool b = false;
            jjson_bool(t, &b);
            doc->debug_draw_water_mask = b;
        }
    }

    return true;
}

static bool jungle_load_cpu_assets(jungle_document_t *doc, char *errbuf, size_t errbuf_sz)
{
    jungle_doc_clear_buffers(doc);

    if (doc->terrain_heightmap[0]) {
        if (!TerrainAsset_LoadHeightmapCPU(doc->terrain_heightmap,
                                           doc->terrain_width, doc->terrain_height,
                                           doc->terrain_height_format,
                                           &doc->cpu_heightmap, &doc->cpu_heightmap_bytes,
                                           errbuf, errbuf_sz)) {
            return false;
        }
    }

    if (doc->terrain_splatmap[0]) {
        if (!TerrainAsset_LoadTextureCPU8(doc->terrain_splatmap,
                                          &doc->cpu_splat, &doc->cpu_splat_bytes,
                                          &doc->cpu_splat_w, &doc->cpu_splat_h,
                                          &doc->cpu_splat_comp,
                                          errbuf, errbuf_sz)) {
            return false;
        }
    }

    if (doc->water_mask[0]) {
        if (!TerrainAsset_LoadTextureCPU8(doc->water_mask,
                                          &doc->cpu_water_mask, &doc->cpu_water_mask_bytes,
                                          &doc->cpu_water_mask_w, &doc->cpu_water_mask_h,
                                          &doc->cpu_water_mask_comp,
                                          errbuf, errbuf_sz)) {
            return false;
        }
    }

    if (doc->seam_patch_paths_count > 0) {
        const char *const *pp = (const char *const *)doc->seam_patch_paths;
        if (!TerrainSeam_ValidatePatchList(pp, doc->seam_patch_paths_count, errbuf, errbuf_sz))
            return false;
    }

    return true;
}

bool TerrainJungle_LoadFromBuffer(const char *utf8, size_t len,
                                  jungle_document_t **out_doc,
                                  char *errbuf, size_t errbuf_sz)
{
    jjson_value_t *root = nullptr;
    if (!jjson_parse(utf8, len, &root, errbuf, errbuf_sz)) {
        return false;
    }

    jungle_document_t *doc = (jungle_document_t *)Z_TagMallocz(sizeof(jungle_document_t), TAG_RENDERER);

    if (!jungle_fill_from_json(doc, root, errbuf, errbuf_sz)) {
        jjson_free(root);
        TerrainJungle_FreeDocument(doc);
        return false;
    }

    jjson_free(root);

    if (!jungle_load_cpu_assets(doc, errbuf, errbuf_sz)) {
        TerrainJungle_FreeDocument(doc);
        return false;
    }

    *out_doc = doc;
    return true;
}

void TerrainJungle_PrintDocument(const jungle_document_t *d)
{
    if (!d) {
        Com_Printf("[TERRAIN] (null document)\n");
        return;
    }
    Com_Printf("[TERRAIN] version %d format %s mode %d\n",
               d->version, d->format_str, (int)d->mode);
    Com_Printf("[TERRAIN] world.bsp \"%s\" interior_only %d\n",
               d->world_bsp, d->world_bsp_interior_only ? 1 : 0);
    Com_Printf("[TERRAIN] terrain hm \"%s\" %dx%d fmt \"%s\" scales %.3f %.3f origin %.1f %.1f %.1f\n",
               d->terrain_heightmap, d->terrain_width, d->terrain_height, d->terrain_height_format,
               d->terrain_scale_xy, d->terrain_scale_z,
               d->terrain_origin[0], d->terrain_origin[1], d->terrain_origin[2]);
    Com_Printf("[TERRAIN] chunk %d lod %d morph %d splat \"%s\"\n",
               d->terrain_chunk_size, d->terrain_lod_count, d->terrain_lod_morph ? 1 : 0,
               d->terrain_splatmap);
    Com_Printf("[TERRAIN] materials %d seam_patches %d\n",
               d->terrain_materials_count, d->seam_patch_paths_count);
    Com_Printf("[TERRAIN] water en %d level %.3f mask \"%s\"\n",
               d->water_enabled ? 1 : 0, d->water_level, d->water_mask);
    Com_Printf("[TERRAIN] CPU hm %zu bytes splat %zu wm %zu\n",
               d->cpu_heightmap_bytes, d->cpu_splat_bytes, d->cpu_water_mask_bytes);
}

static jungle_document_t *s_loaded_document = nullptr;

bool TerrainJungle_LoadFromVFS(const char *vfs_path, char *errbuf, size_t errbuf_sz)
{
    void *buf = nullptr;
    int len = FS_LoadFile(vfs_path, &buf);
    if (len < 0 || !buf) {
        if (errbuf && errbuf_sz)
            Q_snprintf(errbuf, errbuf_sz, "could not read \"%s\"", vfs_path);
        return false;
    }

    jungle_document_t *doc = nullptr;
    bool ok = TerrainJungle_LoadFromBuffer((const char *)buf, (size_t)len, &doc, errbuf, errbuf_sz);
    Z_Free(buf);

    if (!ok)
        return false;

    if (s_loaded_document)
        TerrainJungle_FreeDocument(s_loaded_document);
    s_loaded_document = doc;
    return true;
}

void TerrainJungle_UnloadAll(void)
{
    if (s_loaded_document) {
        TerrainJungle_FreeDocument(s_loaded_document);
        s_loaded_document = nullptr;
    }
}

void TerrainJungle_PrintLoaded(void)
{
    TerrainJungle_PrintDocument(s_loaded_document);
}

const jungle_document_t *TerrainJungle_GetLoaded(void)
{
    return s_loaded_document;
}
