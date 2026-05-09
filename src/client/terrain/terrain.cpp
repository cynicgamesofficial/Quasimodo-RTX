/*
 * Quasimodo RTX — terrain subsystem (Phase 1 skeleton only).
 */

#include "terrain.h"

extern "C" {
#include "common/cmd.h"
#include "common/cvar.h"
}

static bool terrain_registered = false;
static bool terrain_loaded = false;

cvar_t *terrain_enable;
cvar_t *terrain_collision;
cvar_t *terrain_water;
cvar_t *terrain_debug;
cvar_t *terrain_wireframe;
cvar_t *terrain_show_chunks;
cvar_t *terrain_show_lod;
cvar_t *terrain_show_seams;
cvar_t *terrain_water_debug;
cvar_t *terrain_water_level;
cvar_t *terrain_lod_bias;

static bool Terrain_IsSubsystemEnabled(void)
{
    return terrain_enable && terrain_enable->integer;
}

static bool Terrain_IsCollisionEnabled(void)
{
    return terrain_collision && terrain_collision->integer;
}

static void Terrain_RegisterVarsAndCommands(void);

static void Terrain_Cmd_Load_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Com_Printf("[TERRAIN] terrain loading not implemented in Phase 1\n");
}

static void Terrain_Cmd_Unload_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Terrain_Unload();
    Com_Printf("[TERRAIN] terrain unloaded (Phase 1 placeholder)\n");
}

static void Terrain_Cmd_Reload_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Com_Printf("[TERRAIN] terrain_reload not implemented in Phase 1\n");
}

static void Terrain_Cmd_Info_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    Com_Printf("[TERRAIN] Phase 1 skeleton\n");
    Com_Printf("[TERRAIN] terrain_enable: %d\n", Terrain_IsSubsystemEnabled() ? 1 : 0);
    Com_Printf("[TERRAIN] loaded: %s\n", terrain_loaded ? "yes" : "no");
}

static void Terrain_Cmd_Probe_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Com_Printf("[TERRAIN] terrain_probe not implemented in Phase 1\n");
}

static void Terrain_Cmd_DumpChunks_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Com_Printf("[TERRAIN] terrain_dump_chunks not implemented in Phase 1\n");
}

static void Terrain_Cmd_Rebuild_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
    Com_Printf("[TERRAIN] terrain_rebuild not implemented in Phase 1\n");
}

static void Terrain_RegisterVarsAndCommands(void)
{
    terrain_enable = Cvar_Get("terrain_enable", "0", CVAR_ARCHIVE);
    terrain_collision = Cvar_Get("terrain_collision", "0", CVAR_ARCHIVE);
    terrain_water = Cvar_Get("terrain_water", "0", CVAR_ARCHIVE);
    terrain_debug = Cvar_Get("terrain_debug", "0", 0);
    terrain_wireframe = Cvar_Get("terrain_wireframe", "0", 0);
    terrain_show_chunks = Cvar_Get("terrain_show_chunks", "0", 0);
    terrain_show_lod = Cvar_Get("terrain_show_lod", "0", 0);
    terrain_show_seams = Cvar_Get("terrain_show_seams", "0", 0);
    terrain_water_debug = Cvar_Get("terrain_water_debug", "0", 0);
    terrain_water_level = Cvar_Get("terrain_water_level", "0", CVAR_ARCHIVE);
    terrain_lod_bias = Cvar_Get("terrain_lod_bias", "0", CVAR_ARCHIVE);

    Cmd_AddCommand("terrain_load", Terrain_Cmd_Load_f);
    Cmd_AddCommand("terrain_unload", Terrain_Cmd_Unload_f);
    Cmd_AddCommand("terrain_reload", Terrain_Cmd_Reload_f);
    Cmd_AddCommand("terrain_info", Terrain_Cmd_Info_f);
    Cmd_AddCommand("terrain_probe", Terrain_Cmd_Probe_f);
    Cmd_AddCommand("terrain_dump_chunks", Terrain_Cmd_DumpChunks_f);
    Cmd_AddCommand("terrain_rebuild", Terrain_Cmd_Rebuild_f);
}

static void Terrain_UnregisterCommands(void)
{
    Cmd_RemoveCommand("terrain_load");
    Cmd_RemoveCommand("terrain_unload");
    Cmd_RemoveCommand("terrain_reload");
    Cmd_RemoveCommand("terrain_info");
    Cmd_RemoveCommand("terrain_probe");
    Cmd_RemoveCommand("terrain_dump_chunks");
    Cmd_RemoveCommand("terrain_rebuild");
}

void Terrain_Init(void)
{
    if (terrain_registered) {
        return;
    }

    Terrain_RegisterVarsAndCommands();
    terrain_registered = true;
    Com_Printf("[TERRAIN] terrain system initialized\n");
}

void Terrain_Shutdown(void)
{
    if (!terrain_registered) {
        return;
    }

    Terrain_Unload();
    Terrain_UnregisterCommands();
    terrain_registered = false;
}

bool Terrain_LoadJungle(const char *mapname)
{
    (void)mapname;

    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return false;
    }

    Com_Printf("[TERRAIN] terrain loading not implemented in Phase 1\n");
    return false;
}

void Terrain_Unload(void)
{
    terrain_loaded = false;
}

bool Terrain_IsLoaded(void)
{
    return terrain_loaded;
}

void Terrain_PerFrameBegin(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_PerFrameEnd(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_InstanceBLAS(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_TraceLine(trace_t *trace,
                       const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs,
                       int brushmask)
{
    (void)start;
    (void)end;
    (void)mins;
    (void)maxs;
    (void)brushmask;

    if (!terrain_registered || !Terrain_IsSubsystemEnabled() || !Terrain_IsCollisionEnabled()) {
        return;
    }

    (void)trace;
}

int Terrain_PointContents(const vec3_t p)
{
    (void)p;

    if (!terrain_registered || !Terrain_IsSubsystemEnabled() || !Terrain_IsCollisionEnabled()) {
        return 0;
    }

    return 0;
}

void Terrain_OnSwapchainRecreate(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_OnPipelineReload(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}
