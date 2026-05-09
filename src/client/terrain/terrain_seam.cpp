/*
 * Quasimodo RTX — seam .patch text parse + CPU tessellation (Phase 3).
 *
 * See terrain_seam.cpp Phase 2 header contract; extended with grid/points blocks.
 */

#include "terrain.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/files.h"
#include "common/zone.h"
}

typedef struct {
    float *xyz;          /* 3 * num_vertices */
    float *uv;           /* 2 * num_vertices */
    uint32_t *indices;
    int num_vertices;
    int num_indices;
    char material[MAX_QPATH];
    char align[32];
} terrain_seam_cpu_mesh_t;

static terrain_seam_cpu_mesh_t *g_seam_meshes = nullptr;
static int g_seam_mesh_count = 0;

static void terrain_seam_free_one(terrain_seam_cpu_mesh_t *m)
{
    if (!m)
        return;
    if (m->xyz)
        Z_Free(m->xyz);
    if (m->uv)
        Z_Free(m->uv);
    if (m->indices)
        Z_Free(m->indices);
    m->xyz = nullptr;
    m->uv = nullptr;
    m->indices = nullptr;
    m->num_vertices = 0;
    m->num_indices = 0;
    m->material[0] = '\0';
    m->align[0] = '\0';
}

void TerrainSeam_FreeAll(void)
{
    for (int i = 0; i < g_seam_mesh_count; i++)
        terrain_seam_free_one(&g_seam_meshes[i]);
    if (g_seam_meshes)
        Z_Free(g_seam_meshes);
    g_seam_meshes = nullptr;
    g_seam_mesh_count = 0;
}

static void trim_inplace(char *s)
{
    if (!s)
        return;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static const char *line_end(const char *p, const char *end)
{
    while (p < end && *p != '\n' && *p != '\r')
        p++;
    return p;
}

static bool patch_header_ok(const char *s, int len)
{
    if (len < 13 || strncmp(s, "JUNGLE_PATCH", 12))
        return false;
    const char *p = s + 12;
    while (p < s + len && (*p == ' ' || *p == '\t'))
        p++;
    if (p >= s + len || *p != '1')
        return false;
    p++;
    while (p < s + len && (*p == ' ' || *p == '\t'))
        p++;
    return (p >= s + len || *p == '\r' || *p == '\n');
}

bool TerrainSeam_ValidatePatchFile(const char *vfs_path, char *errbuf, size_t errbuf_sz)
{
    if (!vfs_path || !vfs_path[0]) {
        Q_snprintf(errbuf, errbuf_sz, "empty patch path");
        return false;
    }

    void *buf = nullptr;
    int len = FS_LoadFile(vfs_path, &buf);
    if (len < 0 || !buf) {
        Q_snprintf(errbuf, errbuf_sz, "cannot read \"%s\"", vfs_path);
        return false;
    }

    const char *base = (const char *)buf;
    const char *end = base + len;
    const char *scan = base;
    bool ok = false;

    while (scan < end) {
        const char *le = line_end(scan, end);
        char line[2048];
        size_t ln = (size_t)(le - scan);
        if (ln >= sizeof line)
            ln = sizeof line - 1;
        memcpy(line, scan, ln);
        line[ln] = '\0';
        trim_inplace(line);
        scan = le;
        if (scan < end && *scan == '\r')
            scan++;
        if (scan < end && *scan == '\n')
            scan++;

        if (!line[0] || line[0] == '#')
            continue;

        ok = patch_header_ok(line, (int)strlen(line));
        break;
    }

    Z_Free(buf);

    if (!ok)
        Q_snprintf(errbuf, errbuf_sz, "\"%s\": missing header \"JUNGLE_PATCH 1\"", vfs_path);

    return ok;
}

bool TerrainSeam_ValidatePatchList(const char *const *paths, int count,
                                   char *errbuf, size_t errbuf_sz)
{
    if (!paths || count <= 0)
        return true;

    for (int i = 0; i < count; i++) {
        if (!TerrainSeam_ValidatePatchFile(paths[i], errbuf, errbuf_sz))
            return false;
    }
    return true;
}

static bool parse_patch_and_tessellate(const char *vfs_path, terrain_seam_cpu_mesh_t *out_mesh,
                                       char *errbuf, size_t errbuf_sz)
{
    if (!vfs_path || !out_mesh || !errbuf || errbuf_sz == 0) {
        if (errbuf && errbuf_sz)
            Q_snprintf(errbuf, errbuf_sz, "invalid arguments");
        return false;
    }

    memset(out_mesh, 0, sizeof(*out_mesh));

    void *buf = nullptr;
    int len = FS_LoadFile(vfs_path, &buf);
    if (len < 0 || !buf) {
        Q_snprintf(errbuf, errbuf_sz, "cannot read \"%s\"", vfs_path);
        return false;
    }

    const char *base = (const char *)buf;
    const char *end = base + len;

    int gw = 0, gh = 0;
    char material[MAX_QPATH] = "";
    char align[32] = "free";
    bool saw_header = false;
    bool saw_grid = false;
    bool saw_points_kw = false;

    const char *scan = base;

    while (scan < end) {
        const char *le = line_end(scan, end);
        char line[2048];
        size_t ln = (size_t)(le - scan);
        if (ln >= sizeof line)
            ln = sizeof line - 1;
        memcpy(line, scan, ln);
        line[ln] = '\0';
        trim_inplace(line);

        if (!line[0] || line[0] == '#') {
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            continue;
        }

        if (!saw_header) {
            if (!patch_header_ok(scan, (int)(le - scan))) {
                Q_snprintf(errbuf, errbuf_sz, "\"%s\": expected header \"JUNGLE_PATCH 1\" first", vfs_path);
                Z_Free(buf);
                Com_EPrintf("[TERRAIN] %s\n", errbuf);
                return false;
            }
            saw_header = true;
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            continue;
        }

        if (sscanf(line, "grid %d %d", &gw, &gh) == 2) {
            if (gw <= 1 || gh <= 1) {
                Q_snprintf(errbuf, errbuf_sz, "\"%s\": grid dimensions must be > 1", vfs_path);
                Z_Free(buf);
                Com_EPrintf("[TERRAIN] %s\n", errbuf);
                return false;
            }
            saw_grid = true;
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            continue;
        }

        if (!strncmp(line, "material", 8) && (line[8] == '\0' || isspace((unsigned char)line[8]))) {
            const char *p = line + 8;
            while (*p && isspace((unsigned char)*p))
                p++;
            Q_strlcpy(material, p, sizeof material);
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            continue;
        }

        if (!strncmp(line, "align", 5) && (line[5] == '\0' || isspace((unsigned char)line[5]))) {
            char a[32];
            if (sscanf(line + 5, "%31s", a) != 1) {
                Q_snprintf(errbuf, errbuf_sz, "\"%s\": align needs bsp_face or free", vfs_path);
                Z_Free(buf);
                Com_EPrintf("[TERRAIN] %s\n", errbuf);
                return false;
            }
            if (Q_strcasecmp(a, "bsp_face") && Q_strcasecmp(a, "free")) {
                Q_snprintf(errbuf, errbuf_sz, "\"%s\": unknown align \"%s\"", vfs_path, a);
                Z_Free(buf);
                Com_EPrintf("[TERRAIN] %s\n", errbuf);
                return false;
            }
            Q_strlcpy(align, a, sizeof align);
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            continue;
        }

        if (!strcmp(line, "points")) {
            saw_points_kw = true;
            scan = le;
            if (scan < end && *scan == '\r')
                scan++;
            if (scan < end && *scan == '\n')
                scan++;
            break;
        }

        Q_snprintf(errbuf, errbuf_sz, "\"%s\": unexpected line before \"points\": \"%s\"", vfs_path, line);
        Z_Free(buf);
        Com_EPrintf("[TERRAIN] %s\n", errbuf);
        return false;
    }

    if (!saw_grid || !saw_points_kw || gw <= 1 || gh <= 1) {
        Q_snprintf(errbuf, errbuf_sz, "\"%s\": missing grid or points block", vfs_path);
        Z_Free(buf);
        Com_EPrintf("[TERRAIN] %s\n", errbuf);
        return false;
    }

    const int need_pts = gw * gh;
    float *pts = (float *)Z_Malloc((size_t)need_pts * 5 * sizeof(float));
    if (!pts) {
        Q_snprintf(errbuf, errbuf_sz, "\"%s\": allocation failed (points)", vfs_path);
        Z_Free(buf);
        Com_EPrintf("[TERRAIN] %s\n", errbuf);
        return false;
    }

    int read_pt = 0;
    while (scan < end && read_pt < need_pts) {
        const char *le = line_end(scan, end);
        char line[2048];
        size_t ln = (size_t)(le - scan);
        if (ln >= sizeof line)
            ln = sizeof line - 1;
        memcpy(line, scan, ln);
        line[ln] = '\0';
        trim_inplace(line);
        scan = le;
        if (scan < end && *scan == '\r')
            scan++;
        if (scan < end && *scan == '\n')
            scan++;

        if (!line[0] || line[0] == '#')
            continue;

        float x, y, z, u, v;
        if (sscanf(line, "%f %f %f %f %f", &x, &y, &z, &u, &v) != 5) {
            Q_snprintf(errbuf, errbuf_sz, "\"%s\": bad point line \"%s\"", vfs_path, line);
            Z_Free(buf);
            Z_Free(pts);
            Com_EPrintf("[TERRAIN] %s\n", errbuf);
            return false;
        }
        if (!isfinite((double)x) || !isfinite((double)y) || !isfinite((double)z)
            || !isfinite((double)u) || !isfinite((double)v)) {
            Q_snprintf(errbuf, errbuf_sz, "\"%s\": non-finite point values", vfs_path);
            Z_Free(buf);
            Z_Free(pts);
            Com_EPrintf("[TERRAIN] %s\n", errbuf);
            return false;
        }
        pts[(size_t)read_pt * 5 + 0] = x;
        pts[(size_t)read_pt * 5 + 1] = y;
        pts[(size_t)read_pt * 5 + 2] = z;
        pts[(size_t)read_pt * 5 + 3] = u;
        pts[(size_t)read_pt * 5 + 4] = v;
        read_pt++;
    }

    Z_Free(buf);

    if (read_pt != need_pts) {
        Q_snprintf(errbuf, errbuf_sz,
                   "\"%s\": expected %d points, got %d",
                   vfs_path, need_pts, read_pt);
        Z_Free(pts);
        Com_EPrintf("[TERRAIN] %s\n", errbuf);
        return false;
    }

    const int nv = need_pts;
    const int nquad = (gw - 1) * (gh - 1);
    const int nidx = nquad * 6;

    float *xyz = (float *)Z_Malloc((size_t)nv * 3 * sizeof(float));
    float *uv = (float *)Z_Malloc((size_t)nv * 2 * sizeof(float));
    uint32_t *idx = (uint32_t *)Z_Malloc((size_t)nidx * sizeof(uint32_t));
    if (!xyz || !uv || !idx) {
        if (xyz)
            Z_Free(xyz);
        if (uv)
            Z_Free(uv);
        if (idx)
            Z_Free(idx);
        Z_Free(pts);
        Q_snprintf(errbuf, errbuf_sz, "\"%s\": allocation failed", vfs_path);
        Com_EPrintf("[TERRAIN] %s\n", errbuf);
        return false;
    }

    for (int i = 0; i < nv; i++) {
        xyz[i * 3 + 0] = pts[(size_t)i * 5 + 0];
        xyz[i * 3 + 1] = pts[(size_t)i * 5 + 1];
        xyz[i * 3 + 2] = pts[(size_t)i * 5 + 2];
        uv[i * 2 + 0] = pts[(size_t)i * 5 + 3];
        uv[i * 2 + 1] = pts[(size_t)i * 5 + 4];
    }

    int wptr = 0;
    for (int j = 0; j < gh - 1; j++) {
        for (int i = 0; i < gw - 1; i++) {
            const uint32_t i00 = (uint32_t)(j * gw + i);
            const uint32_t i10 = (uint32_t)(j * gw + i + 1);
            const uint32_t i01 = (uint32_t)((j + 1) * gw + i);
            const uint32_t i11 = (uint32_t)((j + 1) * gw + i + 1);

            idx[wptr++] = i00;
            idx[wptr++] = i10;
            idx[wptr++] = i11;

            idx[wptr++] = i00;
            idx[wptr++] = i11;
            idx[wptr++] = i01;
        }
    }

    out_mesh->xyz = xyz;
    out_mesh->uv = uv;
    out_mesh->indices = idx;
    out_mesh->num_vertices = nv;
    out_mesh->num_indices = wptr;
    Q_strlcpy(out_mesh->material, material, sizeof out_mesh->material);
    Q_strlcpy(out_mesh->align, align, sizeof out_mesh->align);

    Z_Free(pts);
    return true;
}

bool TerrainSeam_LoadFromPatchPaths(const char *const *paths, int count)
{
    TerrainSeam_FreeAll();

    if (!paths || count <= 0)
        return true;

    g_seam_meshes = (terrain_seam_cpu_mesh_t *)Z_Malloc(sizeof(terrain_seam_cpu_mesh_t) * (size_t)count);
    if (!g_seam_meshes)
        return false;
    g_seam_mesh_count = count;

    for (int i = 0; i < count; i++) {
        memset(&g_seam_meshes[i], 0, sizeof(g_seam_meshes[i]));
        char err[512];
        if (!parse_patch_and_tessellate(paths[i], &g_seam_meshes[i], err, sizeof err)) {
            TerrainSeam_FreeAll();
            return false;
        }
    }

    return true;
}

#endif /* QUASIMODO_TERRAIN */
