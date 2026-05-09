/*
 * Quasimodo RTX — CPU heightfield sampling (Phase 3).
 *
 * Decodes Phase 2 raw buffers; bilinear height; finite-difference normals.
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
}

typedef enum {
    HFMT_UINT8,
    HFMT_UINT16_LE,
    HFMT_UINT16_BE,
    HFMT_FLOAT32
} terrain_hfmt_t;

static bool terrain_parse_height_format(const char *s, terrain_hfmt_t *out)
{
    if (!s || !out)
        return false;
    if (!Q_strcasecmp(s, "uint8")) {
        *out = HFMT_UINT8;
        return true;
    }
    if (!Q_strcasecmp(s, "uint16_le")) {
        *out = HFMT_UINT16_LE;
        return true;
    }
    if (!Q_strcasecmp(s, "uint16_be")) {
        *out = HFMT_UINT16_BE;
        return true;
    }
    if (!Q_strcasecmp(s, "float32")) {
        *out = HFMT_FLOAT32;
        return true;
    }
    return false;
}

static size_t terrain_hfmt_bytes(terrain_hfmt_t f)
{
    switch (f) {
    case HFMT_UINT8:
        return 1;
    case HFMT_UINT16_LE:
    case HFMT_UINT16_BE:
        return 2;
    case HFMT_FLOAT32:
        return 4;
    }
    return 0;
}

static bool terrain_decode_texel(const terrain_heightfield_cpu_t *hf, terrain_hfmt_t fmt,
                                 int ix, int iy, float *out_norm01)
{
    if (!hf || !hf->pixels || !out_norm01)
        return false;
    if (ix < 0 || iy < 0 || ix >= hf->width || iy >= hf->height)
        return false;

    size_t bps = terrain_hfmt_bytes(fmt);
    size_t idx = (size_t)iy * (size_t)hf->width + (size_t)ix;
    size_t off = idx * bps;
    if (off + bps > hf->pixel_bytes)
        return false;

    const uint8_t *p = hf->pixels + off;

    switch (fmt) {
    case HFMT_UINT8:
        *out_norm01 = (float)p[0] * (1.0f / 255.0f);
        return isfinite((double)*out_norm01) != 0;
    case HFMT_UINT16_LE: {
        uint16_t v = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        *out_norm01 = (float)v * (1.0f / 65535.0f);
        return isfinite((double)*out_norm01) != 0;
    }
    case HFMT_UINT16_BE: {
        uint16_t v = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
        *out_norm01 = (float)v * (1.0f / 65535.0f);
        return isfinite((double)*out_norm01) != 0;
    }
    case HFMT_FLOAT32: {
        float fbits;
        memcpy(&fbits, p, sizeof(float));
        *out_norm01 = fbits;
        return isfinite((double)*out_norm01) != 0;
    }
    }
    return false;
}

static bool terrain_world_z_from_norm(const terrain_heightfield_cpu_t *hf, terrain_hfmt_t fmt,
                                      float norm_or_raw, float *out_z)
{
    if (!hf || !out_z)
        return false;
    (void)fmt;
    /*
     * Integer formats store normalized height in [0,1]; float32 stores raw elevation delta
     * before scale_z (matches jungle terrain_scale_z application in Phase 2).
     */
    *out_z = hf->origin[2] + norm_or_raw * hf->scale_z;
    return isfinite((double)*out_z) != 0;
}

bool TerrainHeightmap_SampleTexel(const terrain_heightfield_cpu_t *hf, int ix, int iy, float *out_z)
{
    if (!hf || !out_z)
        return false;
    terrain_hfmt_t fmt;
    if (!terrain_parse_height_format(hf->height_format, &fmt))
        return false;
    float n = 0.f;
    if (!terrain_decode_texel(hf, fmt, ix, iy, &n))
        return false;
    return terrain_world_z_from_norm(hf, fmt, n, out_z);
}

bool TerrainHeightmap_SampleHeight(const terrain_heightfield_cpu_t *hf, float world_x, float world_y,
                                   float *out_z)
{
    if (!hf || !out_z || hf->width < 2 || hf->height < 2)
        return false;
    if (!hf->pixels || hf->pixel_bytes == 0)
        return false;

    terrain_hfmt_t fmt;
    if (!terrain_parse_height_format(hf->height_format, &fmt))
        return false;

    const float ox = hf->origin[0];
    const float oy = hf->origin[1];
    const float sxy = hf->scale_xy;
    if (!(sxy > 0.f))
        return false;

    /* Continuous coordinates on vertex grid 0 .. (w-1), 0 .. (h-1). */
    const float gx = (world_x - ox) / sxy;
    const float gy = (world_y - oy) / sxy;

    const float maxx = (float)(hf->width - 1);
    const float maxy = (float)(hf->height - 1);
    if (gx < 0.f || gx > maxx || gy < 0.f || gy > maxy)
        return false;

    const int ix0 = (int)floorf(gx);
    const int iy0 = (int)floorf(gy);
    int ix1 = ix0 + 1;
    int iy1 = iy0 + 1;
    if (ix1 > hf->width - 1)
        ix1 = hf->width - 1;
    if (iy1 > hf->height - 1)
        iy1 = hf->height - 1;

    const float fx = gx - (float)ix0;
    const float fy = gy - (float)iy0;

    float z00, z10, z01, z11;
    if (!terrain_decode_texel(hf, fmt, ix0, iy0, &z00))
        return false;
    if (!terrain_decode_texel(hf, fmt, ix1, iy0, &z10))
        return false;
    if (!terrain_decode_texel(hf, fmt, ix0, iy1, &z01))
        return false;
    if (!terrain_decode_texel(hf, fmt, ix1, iy1, &z11))
        return false;

    float w00, w10, w01, w11;
    if (!terrain_world_z_from_norm(hf, fmt, z00, &w00))
        return false;
    if (!terrain_world_z_from_norm(hf, fmt, z10, &w10))
        return false;
    if (!terrain_world_z_from_norm(hf, fmt, z01, &w01))
        return false;
    if (!terrain_world_z_from_norm(hf, fmt, z11, &w11))
        return false;

    const float z0 = w00 * (1.f - fx) + w10 * fx;
    const float z1 = w01 * (1.f - fx) + w11 * fx;
    *out_z = z0 * (1.f - fy) + z1 * fy;
    return isfinite((double)*out_z) != 0;
}

bool TerrainHeightmap_SampleNormal(const terrain_heightfield_cpu_t *hf, float world_x, float world_y,
                                   vec3_t out_normal)
{
    if (!hf || hf->width < 2 || hf->height < 2 || !out_normal)
        return false;
    out_normal[0] = 0.f;
    out_normal[1] = 0.f;
    out_normal[2] = 1.f;

    const float sxy = hf->scale_xy;
    if (!(sxy > 0.f))
        return false;

    /* Finite differences in world units; epsilon ~ one half texel width (stable on edges). */
    const float eps = sxy * 0.5f;
    float hc, hxp, hxm, hyp, hym;
    if (!TerrainHeightmap_SampleHeight(hf, world_x, world_y, &hc))
        return false;
    if (!TerrainHeightmap_SampleHeight(hf, world_x + eps, world_y, &hxp))
        return false;
    if (!TerrainHeightmap_SampleHeight(hf, world_x - eps, world_y, &hxm))
        return false;
    if (!TerrainHeightmap_SampleHeight(hf, world_x, world_y + eps, &hyp))
        return false;
    if (!TerrainHeightmap_SampleHeight(hf, world_x, world_y - eps, &hym))
        return false;

    const float dzx = (hxp - hxm) / (2.f * eps);
    const float dzy = (hyp - hym) / (2.f * eps);

    vec3_t n;
    n[0] = -dzx;
    n[1] = -dzy;
    n[2] = 1.f;
    if (VectorNormalize(n) < 1e-8f) {
        out_normal[0] = 0.f;
        out_normal[1] = 0.f;
        out_normal[2] = 1.f;
        return true;
    }
    VectorCopy(n, out_normal);
    return true;
}

extern "C" bool TerrainHeightmap_ValidateCpuBuffer(int width, int height, const char *height_format,
                                                   size_t pixel_bytes)
{
    if (width < 2 || height < 2 || !height_format || !height_format[0])
        return false;

    terrain_hfmt_t fmt;
    if (!terrain_parse_height_format(height_format, &fmt))
        return false;

    const size_t bps = terrain_hfmt_bytes(fmt);
    const size_t need = (size_t)width * (size_t)height * bps;
    return pixel_bytes >= need;
}

#endif /* QUASIMODO_TERRAIN */