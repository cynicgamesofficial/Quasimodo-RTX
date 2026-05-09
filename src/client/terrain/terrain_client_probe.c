/*
 * Client view XY for terrain_probe — compiled as C so MSVC parses client.h (anonymous structs / restrict).
 */

#include "../client.h"

#include "common/common.h"

#include "terrain_internal.h"

bool Terrain_Debug_TryGetClientViewXY(float *out_x, float *out_y)
{
    if (!out_x || !out_y)
        return false;
    if (COM_DEDICATED)
        return false;
    if (cls.state != ca_active || cl.frame.number <= 0)
        return false;
    *out_x = cl.refdef.vieworg[0];
    *out_y = cl.refdef.vieworg[1];
    return true;
}
