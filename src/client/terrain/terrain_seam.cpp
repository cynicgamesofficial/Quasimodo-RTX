/*
 * Quasimodo RTX — seam patch references (Phase 2 placeholder).
 *
 * ---- .patch text format (initial spec; tessellation/collision in later phases) ----
 *
 * Line 1 MUST be exactly:
 *   JUNGLE_PATCH 1
 *
 * Following lines (future): optional boundary metadata, vertex blocks, etc.
 * Phase 2 only validates the header line when a patch file is referenced.
 *
 * UTF-8 files. Lines may use Windows or Unix line endings.
 */

#include <cstring>

#include "terrain.h"

extern "C" {
#include "common/files.h"
#include "common/common.h"
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

    const char *s = (const char *)buf;
    bool ok = false;

    /* First line: JUNGLE_PATCH 1 */
    if (len >= 13 && !strncmp(s, "JUNGLE_PATCH", 12)) {
        const char *p = s + 12;
        while (p < s + len && (*p == ' ' || *p == '\t'))
            p++;
        if (p < s + len && *p == '1') {
            p++;
            while (p < s + len && (*p == ' ' || *p == '\t'))
                p++;
            if (p >= s + len || *p == '\r' || *p == '\n')
                ok = true;
        }
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
