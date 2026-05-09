/*
 * Quasimodo RTX — CPU-side terrain asset loading (Phase 2).
 */

#include <cstring>
#include <cstdlib>

#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "terrain.h"

extern "C" {
#include "common/files.h"
#include "common/zone.h"
}

typedef enum {
    ASSET_FMT_UINT8,
    ASSET_FMT_UINT16_LE,
    ASSET_FMT_UINT16_BE,
    ASSET_FMT_FLOAT32
} terrain_asset_raw_fmt_t;

static size_t terrain_asset_bytes_per_sample(terrain_asset_raw_fmt_t fmt)
{
    switch (fmt) {
    case ASSET_FMT_UINT8:
        return 1;
    case ASSET_FMT_UINT16_LE:
    case ASSET_FMT_UINT16_BE:
        return 2;
    case ASSET_FMT_FLOAT32:
        return 4;
    }
    return 0;
}

static bool terrain_asset_parse_format(const char *s, terrain_asset_raw_fmt_t *out)
{
    if (!s || !out)
        return false;
    if (!Q_strcasecmp(s, "uint8")) {
        *out = ASSET_FMT_UINT8;
        return true;
    }
    if (!Q_strcasecmp(s, "uint16_le")) {
        *out = ASSET_FMT_UINT16_LE;
        return true;
    }
    if (!Q_strcasecmp(s, "uint16_be")) {
        *out = ASSET_FMT_UINT16_BE;
        return true;
    }
    if (!Q_strcasecmp(s, "float32")) {
        *out = ASSET_FMT_FLOAT32;
        return true;
    }
    return false;
}

static bool terrain_asset_ext_is_image(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    if (!Q_strcasecmp(dot, ".png"))
        return true;
    if (!Q_strcasecmp(dot, ".jpg"))
        return true;
    if (!Q_strcasecmp(dot, ".jpeg"))
        return true;
    if (!Q_strcasecmp(dot, ".tga"))
        return true;
    if (!Q_strcasecmp(dot, ".bmp"))
        return true;
    if (!Q_strcasecmp(dot, ".hdr"))
        return true;
    return false;
}

/*
 * Load raw height/sample buffer: exact byte count == width * height * bps.
 */
static bool TerrainAsset_LoadRawHeightmap(const char *vfs_path,
                                   int width, int height,
                                   const char *height_format,
                                   uint8_t **out_buf, size_t *out_bytes,
                                   char *errbuf, size_t errbuf_sz)
{
    terrain_asset_raw_fmt_t fmt;
    if (!terrain_asset_parse_format(height_format, &fmt)) {
        Q_snprintf(errbuf, errbuf_sz, "unknown height_format \"%s\"", height_format ? height_format : "");
        return false;
    }

    size_t bps = terrain_asset_bytes_per_sample(fmt);
    size_t expected = (size_t)width * (size_t)height * bps;

    void *data = nullptr;
    int len = FS_LoadFile(vfs_path, &data);
    if (len < 0 || !data) {
        Q_snprintf(errbuf, errbuf_sz, "missing or unreadable \"%s\"", vfs_path);
        return false;
    }

    if ((size_t)len != expected) {
        Z_Free(data);
        Q_snprintf(errbuf, errbuf_sz,
                   "%s: size %d != expected %zu for %dx%d %s",
                   vfs_path, len, expected, width, height, height_format);
        return false;
    }

    *out_buf = (uint8_t *)data;
    *out_bytes = expected;
    return true;
}

/*
 * Decode image to linear float RGBA32F plane (4 floats per texel), rows row-major.
 * Caller frees with Z_Free.
 */
static bool terrain_asset_load_image_rgba32f(const char *vfs_path,
                                            int *out_w, int *out_h,
                                            float **out_pixels,
                                            char *errbuf, size_t errbuf_sz)
{
    void *filedata = nullptr;
    int filelen = FS_LoadFile(vfs_path, &filedata);
    if (filelen < 0 || !filedata) {
        Q_snprintf(errbuf, errbuf_sz, "missing \"%s\"", vfs_path);
        return false;
    }

    int w = 0, h = 0, comp = 0;
    float *pix = stbi_loadf_from_memory((const stbi_uc *)filedata, filelen, &w, &h, &comp, 4);
    Z_Free(filedata);

    if (!pix || w <= 0 || h <= 0) {
        Q_snprintf(errbuf, errbuf_sz, "stbi_loadf failed for \"%s\"", vfs_path);
        return false;
    }

    *out_w = w;
    *out_h = h;
    *out_pixels = pix;
    return true;
}

/*
 * Convert float RGBA to raw format buffer (single channel: R).
 */
static bool terrain_asset_float_rgba_to_raw(const float *pix, int w, int h,
                                            terrain_asset_raw_fmt_t fmt,
                                            uint8_t **out_buf, size_t *out_bytes)
{
    size_t bps = terrain_asset_bytes_per_sample(fmt);
    size_t total = (size_t)w * (size_t)h * bps;
    uint8_t *dst = (uint8_t *)Z_TagMallocz(total, TAG_RENDERER);

    for (int i = 0; i < w * h; i++) {
        float r = pix[i * 4 + 0];
        size_t off = (size_t)i * bps;
        switch (fmt) {
        case ASSET_FMT_UINT8: {
            float c = r;
            if (c < 0.f)
                c = 0.f;
            if (c > 1.f)
                c = 1.f;
            dst[off] = (uint8_t)(c * 255.f + 0.5f);
            break;
        }
        case ASSET_FMT_UINT16_LE: {
            uint16_t v = (uint16_t)(r * 65535.f + 0.5f);
            if (r < 0.f)
                v = 0;
            if (r > 1.f)
                v = 65535;
            dst[off + 0] = (uint8_t)(v & 0xff);
            dst[off + 1] = (uint8_t)((v >> 8) & 0xff);
            break;
        }
        case ASSET_FMT_UINT16_BE: {
            uint16_t v = (uint16_t)(r * 65535.f + 0.5f);
            if (r < 0.f)
                v = 0;
            if (r > 1.f)
                v = 65535;
            dst[off + 0] = (uint8_t)((v >> 8) & 0xff);
            dst[off + 1] = (uint8_t)(v & 0xff);
            break;
        }
        case ASSET_FMT_FLOAT32: {
            memcpy(dst + off, &r, sizeof(float));
            break;
        }
        }
    }

    *out_buf = dst;
    *out_bytes = total;
    return true;
}

/*
 * Public: load heightmap CPU bytes. Supports raw formats or image paths (first channel → normalized).
 */
bool TerrainAsset_LoadHeightmapCPU(const char *vfs_path,
                                   int width, int height,
                                   const char *height_format,
                                   uint8_t **out_buf, size_t *out_bytes,
                                   char *errbuf, size_t errbuf_sz)
{
    if (!vfs_path || !height_format || !out_buf || !out_bytes) {
        Q_snprintf(errbuf, errbuf_sz, "invalid arguments");
        return false;
    }

    terrain_asset_raw_fmt_t fmt;
    if (!terrain_asset_parse_format(height_format, &fmt)) {
        Q_snprintf(errbuf, errbuf_sz, "unknown height_format \"%s\"", height_format);
        return false;
    }

    if (terrain_asset_ext_is_image(vfs_path)) {
        int iw = 0, ih = 0;
        float *pix = nullptr;
        if (!terrain_asset_load_image_rgba32f(vfs_path, &iw, &ih, &pix, errbuf, errbuf_sz))
            return false;

        if (width > 0 && height > 0 && (iw != width || ih != height)) {
            stbi_image_free(pix);
            Q_snprintf(errbuf, errbuf_sz,
                       "%s: image %dx%d does not match declared %dx%d",
                       vfs_path, iw, ih, width, height);
            return false;
        }

        int fw = width > 0 ? width : iw;
        int fh = height > 0 ? height : ih;
        if (fw != iw || fh != ih) {
            stbi_image_free(pix);
            Q_snprintf(errbuf, errbuf_sz, "internal dimension mismatch");
            return false;
        }

        uint8_t *raw = nullptr;
        size_t rz = 0;
        bool ok = terrain_asset_float_rgba_to_raw(pix, iw, ih, fmt, &raw, &rz);
        stbi_image_free(pix);
        if (!ok) {
            Q_snprintf(errbuf, errbuf_sz, "conversion failed");
            return false;
        }
        *out_buf = raw;
        *out_bytes = rz;
        return true;
    }

    if (width <= 0 || height <= 0) {
        Q_snprintf(errbuf, errbuf_sz, "width/height required for raw heightmap \"%s\"", vfs_path);
        return false;
    }

    return TerrainAsset_LoadRawHeightmap(vfs_path, width, height, height_format,
                                         out_buf, out_bytes, errbuf, errbuf_sz);
}

/*
 * Single-channel or RGBA8 — stored as raw 8-bit per texel (RGBA packed if comp==4).
 */
bool TerrainAsset_LoadTextureCPU8(const char *vfs_path,
                                  uint8_t **out_buf, size_t *out_bytes,
                                  int *out_w, int *out_h, int *out_comp,
                                  char *errbuf, size_t errbuf_sz)
{
    void *filedata = nullptr;
    int filelen = FS_LoadFile(vfs_path, &filedata);
    if (filelen < 0 || !filedata) {
        Q_snprintf(errbuf, errbuf_sz, "missing \"%s\"", vfs_path);
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc *pix = stbi_load_from_memory((const stbi_uc *)filedata, filelen, &w, &h, &comp, 0);
    Z_Free(filedata);

    if (!pix || w <= 0 || h <= 0) {
        Q_snprintf(errbuf, errbuf_sz, "stbi_load failed for \"%s\"", vfs_path);
        return false;
    }

    size_t bpp = (size_t)comp;
    size_t total = (size_t)w * (size_t)h * bpp;
    uint8_t *copy = (uint8_t *)Z_TagMalloc(total, TAG_RENDERER);
    memcpy(copy, pix, total);
    stbi_image_free(pix);

    *out_buf = copy;
    *out_bytes = total;
    *out_w = w;
    *out_h = h;
    *out_comp = comp;
    return true;
}

void TerrainAsset_FreeBuffer(void *p)
{
    if (p)
        Z_Free(p);
}
