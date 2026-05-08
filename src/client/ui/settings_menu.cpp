#include "settings_menu.h"
#include "settings_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
typedef int keydest_t;
typedef struct cmdbuf_s cmdbuf_t;
typedef enum {
    FROM_MENU,
    FROM_CONSOLE,
    FROM_CMDLINE,
    FROM_CODE
} from_t;
typedef struct cvar_s {
    char *name;
    char *string;
    char *latched_string;
    int flags;
    int modified;
    float value;
    struct cvar_s *next;
    int integer;
    char *default_string;
} cvar_t;

extern cmdbuf_t cmd_buffer;
cvar_t *Cvar_Get(const char *name, const char *value, int flags);
cvar_t *Cvar_WeakGet(const char *name);
cvar_t *Cvar_Set(const char *var_name, const char *value);
cvar_t *Cvar_SetEx(const char *var_name, const char *value, from_t from);
void Cbuf_AddText(cmdbuf_t *buffer, const char *text);
int Key_EnumBindings(int key, const char *binding);
const char *Key_KeynumToString(int key);
void Key_SetBinding(int key, const char *binding);
void UI_StartSound(int sound);
void Com_LPrintf(int type, const char *fmt, ...);
void CL_WriteConfig(void);
extern cvar_t *ui_rmlui_debug;
}

using namespace SettingsWidgets;

namespace {

constexpr int K_ESCAPE = 27;
constexpr int K_ENTER = 13;
constexpr int K_BACKSPACE = 8;
constexpr int K_DEL = 127;
constexpr int K_TAB = 9;
constexpr int K_SPACE = 32;
constexpr int K_UPARROW = 128;
constexpr int K_DOWNARROW = 129;
constexpr int K_LEFTARROW = 130;
constexpr int K_RIGHTARROW = 131;
constexpr int K_PGDN = 148;
constexpr int K_PGUP = 149;
constexpr int K_HOME = 150;
constexpr int K_END = 151;
constexpr int K_MWHEELDOWN = 210;
constexpr int K_MWHEELUP = 211;
constexpr int QMS_IN = 2;
constexpr int QMS_MOVE = 3;
constexpr int QMS_OUT = 4;
constexpr int CVAR_ARCHIVE = 1;
constexpr int PRINT_DEVELOPER = 2;

using Option = SettingsMenu::Option;
using Def = SettingsMenu::SettingDef;
using Kind = SettingsMenu::Kind;

struct Choice {
    std::string label;
    std::string value;
};

const Option yes_no[] = { {"Off", "0"}, {"On", "1"} };
const Option renderer_opts[] = { {"OpenGL", "0"}, {"RTX", "1"} };
const Option fullscreen_opts[] = { {"Windowed", "0"}, {"Desktop resolution", "1"} };
const Option adjust_fov_opts[] = { {"Vertical-", "0"}, {"Horizontal+", "1"} };
const Option reflex_opts[] = { {"Off", "0"}, {"On", "1"}, {"On + Boost", "2"} };
const Option fps_opts[] = { {"Off", "0"}, {"FPS", "1"}, {"FPS + Scale", "2"} };
const Option dlss_quality_opts[] = { {"Quality", "0"}, {"Balanced", "1"}, {"Performance", "2"}, {"Ultra Performance", "3"}, {"DLAA", "4"} };
const Option taa_opts[] = { {"None", "0"}, {"Temporal AA", "1"}, {"Temporal Upscaling", "2"} };
const Option gi_opts[] = { {"Low", "0.5"}, {"Medium", "1"}, {"High", "2"} };
const Option reflect_opts[] = { {"Off", "0"}, {"1", "1"}, {"2", "2"}, {"4", "4"}, {"8", "8"} };
const Option filtering_opts[] = { {"Anisotropic", "0"}, {"Mixed", "1"}, {"Nearest", "2"} };
const Option thick_glass_opts[] = { {"Disabled", "0"}, {"Photo Mode", "1"}, {"Enabled", "2"} };
const Option projection_opts[] = { {"Perspective", "0"}, {"Panini", "1"}, {"Stereographic", "2"}, {"Cylindrical", "3"}, {"Equirectangular", "4"}, {"Mercator", "5"} };
const Option sky_opts[] = { {"Original env.map", "0"}, {"Earth", "1"}, {"Stroggos", "2"} };
const Option sun_preset_opts[] = { {"Custom", "0"}, {"Current", "1"}, {"12x Current", "2"}, {"Night", "3"}, {"Dawn", "4"}, {"Morning", "5"}, {"Noon", "6"}, {"Evening", "7"}, {"Dusk", "8"} };
const Option sun_gamepad_opts[] = { {"Off", "0"}, {"Left Stick", "1"}, {"Right Stick", "2"} };
const Option water_opts[] = { {"Fallback", "0"}, {"Analytic", "1"}, {"Sine", "2"}, {"Exp Sine", "3"}, {"Gerstner", "4"}, {"FBM", "5"} };
const Option photo_opts[] = { {"No", "0"}, {"Yes", "1"}, {"Yes, Hide GUI", "2"} };
const Option aperture_opts[] = { {"Circle", "0"}, {"Triangle", "3"}, {"Square", "4"}, {"Pentagon", "5"}, {"Hexagon", "6"}, {"Heptagon", "7"}, {"Octagon", "8"} };
const Option restir_debug_opts[] = { {"Off", "0"}, {"W", "1"}, {"M", "2"}, {"Light Type", "3"}, {"Target PDF", "4"}, {"Spatial", "5"}, {"Temporal Lifecycle", "6"} };
const Option nrd_opts[] = { {"ASVGF", "0"}, {"NRD ReLAX", "1"} };
const Option sli_opts[] = { {"Disabled", "0"}, {"When Available", "1"} };
const Option rt_api_opts[] = { {"Auto", "auto"}, {"KHR Ray Query", "query"}, {"KHR Ray Pipeline", "pipeline"} };
const Option tm_debug_opts[] = { {"Off", "0"}, {"Histogram", "1"}, {"Curve", "2"} };
const Option ambient_opts[] = { {"No", "0"}, {"Yes", "1"}, {"Only Player", "2"} };
const Option chat_sound_opts[] = { {"Disabled", "0"}, {"Default", "1"}, {"Alternative", "2"} };
const Option sound_engine_opts[] = { {"No Sound", "0"}, {"Software", "1"}, {"OpenAL", "2"} };
const Option rail_type_opts[] = { {"Default", "0"}, {"Core Only", "1"}, {"Core + Spiral", "2"} };
const Option color_opts[] = { {"Black", "black"}, {"Red", "red"}, {"Green", "green"}, {"Yellow", "yellow"}, {"Blue", "blue"}, {"Cyan", "cyan"}, {"Magenta", "magenta"}, {"White", "white"} };
const Option show_item_opts[] = { {"Do Not Show", "0"}, {"Show On Change", "1"}, {"Always Show", "2"} };
const Option demobar_opts[] = { {"No", "0"}, {"Yes", "1"}, {"Verbose", "2"} };
const Option scale_opts[] = { {"Auto", "0"}, {"1x", "1"}, {"2x", "2"}, {"4x", "4"} };
const Option hand_opts[] = { {"Right", "0"}, {"Left", "1"}, {"Center", "2"} };
const Option aim_opts[] = { {"Default", "0"}, {"At Crosshair", "1"} };
const Option view_opts[] = { {"Nothing", "0"}, {"Just Gun", "1"}, {"First Person Model", "2"}, {"Third Person", "3"} };
const Option protocol_opts[] = { {"Auto", "0"}, {"Q2", "34"}, {"R1Q2", "35"}, {"Q2PRO", "36"} };
const Option crosshair_mode_opts[] = { {"Legacy", "0"}, {"New RmlUi", "1"} };
const Option crosshair_opts[] = { {"None", "0"}, {"Cross", "1"}, {"Dot", "2"}, {"Angle", "3"} };
const Option crosshair_scale_opts[] = { {"0.5x", "0.5"}, {"1x", "1"}, {"2x", "2"} };
const Option rml_crosshair_style_opts[] = { {"Classic", "0"}, {"Dot", "1"}, {"Cross", "2"}, {"Circle", "3"} };

#define OPTS(x) x, (int)(sizeof(x) / sizeof((x)[0]))
#define SEC(tab, text) { tab, Kind::Section, text, text, nullptr, "", 0, 0, 0, 0, false, 0, nullptr, 0, nullptr, nullptr }
#define TOG(tab, sec, label, cv, def) { tab, Kind::Toggle, sec, label, cv, def, 0, 1, 1, 0, false, 0, yes_no, 2, nullptr, nullptr }
#define TOG_DESC(tab, sec, label, cv, def, desc) { tab, Kind::Toggle, sec, label, cv, def, 0, 1, 1, 0, false, 0, yes_no, 2, nullptr, desc }
#define ITOG(tab, sec, label, cv, def) { tab, Kind::Toggle, sec, label, cv, def, 0, 1, 1, 0, true, 0, yes_no, 2, nullptr, nullptr }
#define BIT(tab, sec, label, cv, def, bit, inv) { tab, Kind::Bitmask, sec, label, cv, def, 0, 1, 1, 0, inv, bit, yes_no, 2, nullptr, nullptr }
#define SL(tab, sec, label, cv, def, mn, mx, st, dec) { tab, Kind::Slider, sec, label, cv, def, mn, mx, st, dec, false, 0, nullptr, 0, nullptr, nullptr }
#define DD(tab, sec, label, cv, def, opts) { tab, Kind::Dropdown, sec, label, cv, def, 0, 0, 0, 0, false, 0, OPTS(opts), nullptr, nullptr }
#define TXT(tab, sec, label, cv, def) { tab, Kind::Text, sec, label, cv, def, 0, 0, 0, 0, false, 0, nullptr, 0, nullptr, nullptr }
#define KEY(tab, sec, label, cmd) { tab, Kind::Keybind, sec, label, cmd, "", 0, 0, 0, 0, false, 0, nullptr, 0, nullptr, nullptr }
#define LINK(tab, sec, label, cmd) { tab, Kind::Link, sec, label, nullptr, "", 0, 0, 0, 0, false, 0, nullptr, 0, cmd, nullptr }

const Def settings_defs[] = {
    SEC(SettingsTab::Graphics, "Display"),
    DD(SettingsTab::Graphics, "Display", "Renderer", "vid_rtx", "1", renderer_opts),
    DD(SettingsTab::Graphics, "Display", "Video Mode", "vid_fullscreen", "0", fullscreen_opts),
    DD(SettingsTab::Graphics, "Display", "Fullscreen Display", "vid_display", "0", yes_no),
    DD(SettingsTab::Graphics, "Display", "Vertical Sync", "vid_vsync", "0", yes_no),
    DD(SettingsTab::Graphics, "Display", "NVIDIA Reflex", "cl_reflex", "2", reflex_opts),
    DD(SettingsTab::Graphics, "Display", "FPS Counter", "scr_fps", "0", fps_opts),
    SL(SettingsTab::Graphics, "Display", "Field of View", "fov", "90", 60, 160, 5, 0),
    DD(SettingsTab::Graphics, "Display", "FOV Scaling", "cl_adjustfov", "1", adjust_fov_opts),
    SL(SettingsTab::Graphics, "Rendering", "Fixed Resolution Scale", "viewsize", "100", 25, 200, 5, 0),
    DD(SettingsTab::Graphics, "Rendering", "Dynamic Resolution Scaling", "drs_enable", "0", yes_no),
    SL(SettingsTab::Graphics, "Rendering", "DRS Target FPS", "drs_target", "60", 30, 250, 1, 0),
    SL(SettingsTab::Graphics, "Rendering", "DRS Min Scale", "drs_minscale", "50", 25, 100, 5, 0),
    SL(SettingsTab::Graphics, "Rendering", "DRS Max Scale", "drs_maxscale", "100", 50, 150, 5, 0),

    SEC(SettingsTab::RTX, "Path Tracer"),
    DD(SettingsTab::RTX, "Path Tracer", "DLSS Super Resolution", "pt_dlss", "1", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "DLSS Quality Mode", "pt_dlss_quality", "0", dlss_quality_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Global Illumination", "pt_num_bounce_rays", "1", gi_opts),
    DD(SettingsTab::RTX, "Path Tracer", "ReSTIR Direct Lighting", "pt_restir_di", "0", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "Reflection / Refraction Depth", "pt_reflect_refract", "1", reflect_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Security Cameras", "pt_cameras", "0", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "Caustics", "pt_caustics", "1", yes_no),
    SL(SettingsTab::RTX, "Path Tracer", "Texture LOD Bias", "pt_texture_lod_bias", "0", -2, 2, .5f, 1),
    DD(SettingsTab::RTX, "Path Tracer", "Texture Filtering", "pt_nearest", "0", filtering_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Thick Glass Refraction", "pt_thick_glass", "2", thick_glass_opts),
    TOG(SettingsTab::RTX, "Path Tracer", "Surface Lights", "pt_enable_surface_lights", "1"),
    TOG(SettingsTab::RTX, "Path Tracer", "Warp Surface Lights", "pt_enable_surface_lights_warp", "0"),
    SL(SettingsTab::RTX, "Path Tracer", "Surface Light Threshold", "pt_surface_lights_threshold", "215", 0, 255, 1, 0),
    SL(SettingsTab::RTX, "Path Tracer", "BSP Radiance Scale", "pt_bsp_radiance_scale", "0.001", 0, .01f, .001f, 3),
    SL(SettingsTab::RTX, "Path Tracer", "BSP Sky Lights", "pt_bsp_sky_lights", "0", 0, 10, 1, 0),
    DD(SettingsTab::RTX, "Denoiser", "Denoiser", "flt_enable", "1", yes_no),
    DD(SettingsTab::RTX, "Denoiser", "ReSTIR Denoiser", "pt_nrd", "1", nrd_opts),
    TOG(SettingsTab::RTX, "Denoiser", "ReSTIR DI Spatial Reuse", "pt_restir_di_spatial", "1"),
    DD(SettingsTab::RTX, "Denoiser", "ReSTIR DI Debug View", "pt_restir_di_debug", "0", restir_debug_opts),
    TOG(SettingsTab::RTX, "Denoiser", "Light PDF Correction", "pt_light_stats", "0"),
    DD(SettingsTab::RTX, "Upscaling", "Temporal AA", "flt_taa", "1", taa_opts),
    TOG(SettingsTab::RTX, "Upscaling", "FSR Enabled", "flt_fsr_enable", "0"),
    SL(SettingsTab::RTX, "Upscaling", "FSR Sharpness", "flt_fsr_sharpness", "0.2", 0, 2, .05f, 2),
    SL(SettingsTab::RTX, "Tone Mapping", "Exposure Bias", "tm_exposure_bias", "0", -5, 0, .1f, 1),
    SL(SettingsTab::RTX, "Tone Mapping", "Contrast", "tm_reinhard", "0", 0, 1, .1f, 1),
    DD(SettingsTab::RTX, "Tone Mapping", "Bloom", "bloom_enable", "1", yes_no),
    SL(SettingsTab::RTX, "Tone Mapping", "Bloom Intensity", "bloom_intensity", "0.002", 0, 1, .01f, 3),
    SL(SettingsTab::RTX, "Tone Mapping", "Bloom Sigma", "bloom_sigma", "0.037", 0, .2f, .005f, 3),
    DD(SettingsTab::RTX, "Depth of Field", "Depth of Field", "pt_dof", "1", yes_no),
    DD(SettingsTab::RTX, "Depth of Field", "Aperture Shape", "pt_aperture_type", "0", aperture_opts),
    SL(SettingsTab::RTX, "Depth of Field", "Aperture Rotation", "pt_aperture_angle", "0", 0, 1, .1f, 1),
    DD(SettingsTab::RTX, "Projection", "Projection", "pt_projection", "0", projection_opts),
    DD(SettingsTab::RTX, "Photo Mode", "Accumulation Rendering", "pt_accumulation_rendering", "1", yes_no),
    SL(SettingsTab::RTX, "Photo Mode", "Frames to Accumulate", "pt_accumulation_rendering_framenum", "500", 100, 8000, 100, 0),
    DD(SettingsTab::RTX, "Photo Mode", "Free Camera", "pt_freecam", "1", yes_no),
    DD(SettingsTab::RTX, "Advanced RTX", "God Rays", "gr_enable", "1", yes_no),
    SL(SettingsTab::RTX, "Advanced RTX", "God Ray Intensity", "gr_intensity", "2.0", 0, 10, .1f, 1),
    SL(SettingsTab::RTX, "Advanced RTX", "God Ray Eccentricity", "gr_eccentricity", "0.75", -1, 1, .05f, 2),
    TOG_DESC(SettingsTab::RTX, "NVIDIA Debug", "NVIDIA Streamline Debug Overlay", "r_streamline_imgui", "0",
             "Development builds only. Requires restart. Shows NVIDIA Streamline plugin debug panels for Reflex, DLSS, DLSS-G, and visual stats when available."),
    SL(SettingsTab::RTX, "Advanced RTX", "Sun Elevation", "sun_elevation", "45", -20, 90, 1, 0),
    SL(SettingsTab::RTX, "Advanced RTX", "Sun Azimuth", "sun_azimuth", "345", -20, 380, 5, 0),
    SL(SettingsTab::RTX, "Advanced RTX", "Sun Brightness", "sun_brightness", "10", 0, 50, .5f, 1),
    TOG(SettingsTab::RTX, "Advanced RTX", "Show Sky Geometry", "pt_show_sky", "0"),

    SEC(SettingsTab::Effects, "Particles & Explosions"),
    SL(SettingsTab::Effects, "Particles & Explosions", "Particle Multiplier", "cl_particle_num_factor", "1", 0, 4, .1f, 1),
    SL(SettingsTab::Effects, "Particles & Explosions", "Particle Emissive", "pt_particle_emissive", "10.0", 0, 50, .5f, 1),
    TOG(SettingsTab::Effects, "Particles & Explosions", "Explosion Sprites", "cl_explosion_sprites", "1"),
    SL(SettingsTab::Effects, "Particles & Explosions", "Explosion Frametime", "cl_explosion_frametime", "20", 10, 100, 1, 0),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Grenade Explosions", "cl_disable_explosions", "0", 0, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Rocket Explosions", "cl_disable_explosions", "0", 1, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Grenade Particles", "cl_disable_particles", "0", 0, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Rocket Trails", "cl_disable_particles", "0", 3, true),
    ITOG(SettingsTab::Effects, "Entity FX", "Glow on Items", "cl_noglow", "0"),
    ITOG(SettingsTab::Effects, "Entity FX", "Item Bobbing", "cl_nobob", "0"),
    TOG(SettingsTab::Effects, "Screen Effects", "Screen Blending", "tm_blend_enable", "1"),
    SL(SettingsTab::Effects, "Screen Effects", "Blend Center Strength", "tm_blend_scale_center", "0", 0, 1, .05f, 2),
    SL(SettingsTab::Effects, "Screen Effects", "Blend Border Strength", "tm_blend_scale_border", "0", 0, 1, .05f, 2),
    DD(SettingsTab::Effects, "Screen Effects", "Water Warp", "pt_waterwarp", "0", yes_no),
    DD(SettingsTab::Effects, "Rail Gun", "Rail Trail Type", "cl_railtrail_type", "0", rail_type_opts),
    SL(SettingsTab::Effects, "Rail Gun", "Rail Trail Duration", "cl_railtrail_time", "1.0", .1f, 3, .1f, 1),
    SL(SettingsTab::Effects, "Rail Gun", "Core Width", "cl_railcore_width", "2", 1, 6, 1, 0),
    SL(SettingsTab::Effects, "Rail Gun", "Spiral Radius", "cl_railspiral_radius", "3", 1, 6, 1, 0),
    DD(SettingsTab::Effects, "Rail Gun", "Core Color", "cl_railcore_color", "red", color_opts),
    DD(SettingsTab::Effects, "Rail Gun", "Spiral Color", "cl_railspiral_color", "blue", color_opts),

    SEC(SettingsTab::Audio, "Volume"),
    SL(SettingsTab::Audio, "Volume", "Master Volume", "s_volume", "1", 0, 1, .05f, 2),
    SL(SettingsTab::Audio, "Volume", "Music Volume", "ogg_volume", "1", 0, 1, .05f, 2),
    TOG(SettingsTab::Audio, "Volume", "Music Enabled", "ogg_enable", "1"),
    TOG(SettingsTab::Audio, "Volume", "Title Menu Track", "ui_title_music", "1"),
    TOG(SettingsTab::Audio, "Volume", "Shuffle Tracks", "ogg_shuffle", "0"),
    TOG(SettingsTab::Audio, "Sound Engine", "Underwater Effect", "s_underwater", "1"),
    DD(SettingsTab::Audio, "Sound Engine", "Ambient Sounds", "s_ambient", "1", ambient_opts),
    DD(SettingsTab::Audio, "Sound Engine", "Chat Beep", "cl_chat_sound", "1", chat_sound_opts),
    DD(SettingsTab::Audio, "Sound Engine", "Sound Engine", "s_enable", "2", sound_engine_opts),
    TXT(SettingsTab::Audio, "Sound Engine", "OpenAL Device", "al_device", ""),
    SL(SettingsTab::Audio, "Sound Engine", "Mix Ahead", "s_mixahead", "0.2", .01f, .5f, .01f, 2),

    SEC(SettingsTab::Controls, "Mouse"),
    SL(SettingsTab::Controls, "Mouse", "Mouse Sensitivity", "sensitivity", "3", 1, 30, .1f, 1),
    DD(SettingsTab::Controls, "Mouse", "Invert Mouse", "m_invert", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Adjust Sensitivity with FOV", "m_autosens", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Filter Mouse Input", "m_filter", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Free Look", "freelook", "1", yes_no),
    DD(SettingsTab::Controls, "Movement", "Always Run", "cl_run", "1", yes_no),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Attack", "+attack"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Next Weapon", "weapnext"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Previous Weapon", "weapprev"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Walk Forward", "+forward"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Backpedal", "+back"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Run", "+speed"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Step Left", "+moveleft"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Step Right", "+moveright"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Jump", "+moveup"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Crouch", "+movedown"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Inventory", "inven"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Use Item", "invuse"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Drop Item", "invdrop"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Previous Item", "invprev"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Next Item", "invnext"),
    KEY(SettingsTab::Controls, "Function Keys", "Screenshot", "screenshot"),
    KEY(SettingsTab::Controls, "Function Keys", "Pause Game", "pause"),
    KEY(SettingsTab::Controls, "Function Keys", "Score Table", "score"),
    KEY(SettingsTab::Controls, "Function Keys", "Chat", "messagemode"),
    KEY(SettingsTab::Controls, "Weapons", "Blaster", "use Blaster"),
    KEY(SettingsTab::Controls, "Weapons", "Shotgun", "use Shotgun"),
    KEY(SettingsTab::Controls, "Weapons", "Super Shotgun", "use Super Shotgun"),
    KEY(SettingsTab::Controls, "Weapons", "Machinegun", "use Machinegun"),
    KEY(SettingsTab::Controls, "Weapons", "Chaingun", "use Chaingun"),
    KEY(SettingsTab::Controls, "Weapons", "Grenade Launcher", "use Grenade Launcher"),
    KEY(SettingsTab::Controls, "Weapons", "Rocket Launcher", "use Rocket Launcher"),
    KEY(SettingsTab::Controls, "Weapons", "HyperBlaster", "use HyperBlaster"),
    KEY(SettingsTab::Controls, "Weapons", "Railgun", "use Railgun"),
    KEY(SettingsTab::Controls, "Weapons", "BFG10K", "use BFG10K"),

    SEC(SettingsTab::Game, "Player"),
    TXT(SettingsTab::Game, "Player", "Player Name", "name", "Player"),
    TXT(SettingsTab::Game, "Player", "Player Skin", "skin", "male/grunt"),
    DD(SettingsTab::Game, "Player", "Handedness", "hand", "0", hand_opts),
    DD(SettingsTab::Game, "Player", "Aiming Point", "aimfix", "1", aim_opts),
    DD(SettingsTab::Game, "Player", "View Model", "cl_player_model", "1", view_opts),
    SL(SettingsTab::Game, "Player", "Gun Opacity", "cl_gunalpha", "1", 0, 1, .05f, 2),
    SL(SettingsTab::Game, "Player", "Gun FOV", "cl_gunfov", "90", 0, 180, 5, 0),
    SL(SettingsTab::Game, "Player", "Gun Scale", "cl_gunscale", "0.25", .1f, 2, .05f, 2),
    SL(SettingsTab::Game, "Player", "Gun Offset X", "cl_gun_x", "0", -10, 10, .5f, 1),
    SL(SettingsTab::Game, "Player", "Gun Offset Y", "cl_gun_y", "0", -10, 10, .5f, 1),
    SL(SettingsTab::Game, "Player", "Gun Offset Z", "cl_gun_z", "0", -10, 10, .5f, 1),
    DD(SettingsTab::Game, "HUD", "Selected Item", "scr_showitemname", "1", show_item_opts),
    DD(SettingsTab::Game, "HUD", "Show Pause Plaque", "scr_showpause", "1", yes_no),
    DD(SettingsTab::Game, "HUD", "Ping Graph", "scr_lag_draw", "0", yes_no),
    DD(SettingsTab::Game, "HUD", "Demo Bar", "scr_demobar", "1", demobar_opts),
    SL(SettingsTab::Game, "HUD", "HUD Opacity", "scr_alpha", "1", 0, 1, .05f, 2),
    DD(SettingsTab::Game, "HUD", "HUD Scale", "scr_scale", "0", scale_opts),
    SL(SettingsTab::Game, "HUD", "RmlUi HUD Scale", "ui_rml_hud_scale", "3.0", 0.5f, 6.0f, 0.1f, 1),

    SEC(SettingsTab::Game, "Crosshair"),
    DD(SettingsTab::Game, "Crosshair", "Crosshair Renderer", "ui_crosshair_mode", "1", crosshair_mode_opts),
    DD(SettingsTab::Game, "Crosshair", "New Crosshair Style", "ui_crosshair_style", "0", rml_crosshair_style_opts),
    DD(SettingsTab::Game, "Crosshair", "New Dynamic Crosshair", "ui_crosshair_dynamic", "0", yes_no),
    DD(SettingsTab::Game, "Crosshair", "New Center Dot", "ui_crosshair_dot", "1", yes_no),
    SL(SettingsTab::Game, "Crosshair", "New Outline", "ui_crosshair_outline", "0", 0, 3, 1, 0),
    DD(SettingsTab::Game, "Crosshair", "New T Style", "ui_crosshair_tstyle", "0", yes_no),
    SL(SettingsTab::Game, "Crosshair", "New Size", "ui_crosshair_size", "5", 1, 20, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Thickness", "ui_crosshair_thickness", "1", 1, 5, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Gap", "ui_crosshair_gap", "3", 0, 15, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Opacity", "ui_crosshair_opacity", "0.85", 0, 1, .05f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Red", "ui_crosshair_red", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Green", "ui_crosshair_green", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Blue", "ui_crosshair_blue", "1", 0, 1, .1f, 2),

    SEC(SettingsTab::Game, "Legacy Crosshair"),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Crosshair Type", "crosshair", "1", crosshair_opts),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Crosshair Scale", "ch_scale", "1", crosshair_scale_opts),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Health Color", "ch_health", "0", yes_no),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Red", "ch_red", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Green", "ch_green", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Blue", "ch_blue", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Alpha", "ch_alpha", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy X Offset", "ch_x", "0", -100, 100, 1, 0),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Y Offset", "ch_y", "0", -100, 100, 1, 0),

    DD(SettingsTab::Game, "Gameplay", "Cutscenes", "cl_cinematics", "1", yes_no),
    DD(SettingsTab::Game, "Gameplay", "Gibs", "cl_gibs", "1", yes_no),
    DD(SettingsTab::Game, "Gameplay", "Footsteps", "cl_footsteps", "1", yes_no),

    SEC(SettingsTab::Network, "Protocol"),
    DD(SettingsTab::Network, "Protocol", "Preferred Protocol", "cl_protocol", "0", protocol_opts),
    SL(SettingsTab::Network, "Performance", "Max Packets/sec", "cl_maxpackets", "30", 0, 125, 1, 0),
    SL(SettingsTab::Network, "Performance", "Packet Duplicates", "cl_packetdup", "1", 0, 5, 1, 0),
    TOG(SettingsTab::Network, "Performance", "Instant Packets", "cl_instantpacket", "1"),
    TOG(SettingsTab::Network, "Performance", "Fuzz Hack", "cl_fuzzhack", "0"),
    SL(SettingsTab::Network, "Performance", "Update Rate", "cl_updaterate", "0", 0, 125, 1, 0),
    TXT(SettingsTab::Network, "Downloads", "HTTP Downloads", "cl_http_downloads", "1"),
    TXT(SettingsTab::Network, "Downloads", "HTTP Max Connections", "cl_http_max_connections", "2"),
    TXT(SettingsTab::Network, "Address Book", "Address 0", "adr0", ""),
    TXT(SettingsTab::Network, "Address Book", "Address 1", "adr1", ""),
    TXT(SettingsTab::Network, "Address Book", "Address 2", "adr2", ""),
    TXT(SettingsTab::Network, "Address Book", "Address 3", "adr3", ""),

    SEC(SettingsTab::Advanced, "Environment"),
    DD(SettingsTab::Advanced, "Environment", "Sky Type", "physical_sky", "2", sky_opts),
    SL(SettingsTab::Advanced, "Environment", "Sky Brightness", "physical_sky_brightness", "0", -10, 2, .1f, 1),
    DD(SettingsTab::Advanced, "Environment", "Time of Day", "sun_preset", "0", sun_preset_opts),
    KEY(SettingsTab::Advanced, "Environment", "Next Time of Day", "next_sun"),
    DD(SettingsTab::Advanced, "Environment", "Control Sun with Gamepad", "sun_gamepad", "0", sun_gamepad_opts),
    DD(SettingsTab::Advanced, "Environment", "Clouds", "physical_sky_draw_clouds", "1", yes_no),
    SL(SettingsTab::Advanced, "Environment", "Latitude", "sun_latitude", "32.9", -90, 90, 2, 1),
    DD(SettingsTab::Advanced, "Environment", "Water Mode", "pt_water_fbm", "0", water_opts),
    SL(SettingsTab::Advanced, "Environment", "Water Wave Scale", "pt_water_wave_scale", "1", .1f, 4, .1f, 1),
    SL(SettingsTab::Advanced, "Environment", "Water Wave Speed", "pt_water_wave_speed", "1", 0, 4, .1f, 1),
    SL(SettingsTab::Advanced, "Environment", "Water Wave Steepness", "pt_water_wave_steepness", "1", 1, 4, .1f, 1),
    TOG(SettingsTab::Advanced, "HDR", "HDR", "vid_hdr", "0"),
    SL(SettingsTab::Advanced, "HDR", "Peak Brightness", "tm_hdr_peak_nits", "1000", 100, 2000, 10, 0),
    SL(SettingsTab::Advanced, "HDR", "UI Brightness", "ui_hdr_nits", "300", 100, 2000, 10, 0),
    SL(SettingsTab::Advanced, "HDR", "Saturation", "tm_hdr_saturation_scale", "100", 0, 200, 5, 0),
    TOG(SettingsTab::Advanced, "Developer", "GPU Profiler", "profiler", "0"),
    DD(SettingsTab::Advanced, "Developer", "Ray Tracing API", "ray_tracing_api", "auto", rt_api_opts),
    DD(SettingsTab::Advanced, "Developer", "Tone Mapping Debug", "tm_debug", "0", tm_debug_opts),
    TOG(SettingsTab::Advanced, "Developer", "SVGF Gradient Overlay", "flt_show_gradients", "0"),
    BIT(SettingsTab::Advanced, "Developer", "Fixed Albedo Debug", "flt_fixed_albedo", "0", 1, true),
    DD(SettingsTab::Advanced, "Developer", "Multi-GPU Support", "sli", "1", sli_opts),
};

const Def vanilla_settings_defs[] = {
    SEC(SettingsTab::Game, "Player"),
    TXT(SettingsTab::Game, "Player", "Player Name", "name", "Player"),
    TXT(SettingsTab::Game, "Player", "Player Skin", "skin", "male/grunt"),
    DD(SettingsTab::Game, "Player", "Handedness", "hand", "0", hand_opts),
    DD(SettingsTab::Game, "Player", "Aiming Point", "aimfix", "1", aim_opts),
    DD(SettingsTab::Game, "Player", "View Model", "cl_player_model", "1", view_opts),
    SL(SettingsTab::Game, "Player", "Gun Opacity", "cl_gunalpha", "1", 0, 1, .05f, 2),
    SL(SettingsTab::Game, "Player", "Gun FOV", "cl_gunfov", "90", 0, 180, 5, 0),
    SL(SettingsTab::Game, "Player", "Gun Scale", "cl_gunscale", "0.25", .1f, 2, .05f, 2),
    SL(SettingsTab::Game, "Player", "Gun Offset X", "cl_gun_x", "0", -10, 10, .5f, 1),
    SL(SettingsTab::Game, "Player", "Gun Offset Y", "cl_gun_y", "0", -10, 10, .5f, 1),
    SL(SettingsTab::Game, "Player", "Gun Offset Z", "cl_gun_z", "0", -10, 10, .5f, 1),
    DD(SettingsTab::Game, "Gameplay", "Cutscenes", "cl_cinematics", "1", yes_no),
    DD(SettingsTab::Game, "Gameplay", "Gibs", "cl_gibs", "1", yes_no),
    DD(SettingsTab::Game, "Gameplay", "Footsteps", "cl_footsteps", "1", yes_no),
    DD(SettingsTab::Game, "HUD", "Selected Item", "scr_showitemname", "1", show_item_opts),
    DD(SettingsTab::Game, "HUD", "Show Pause Plaque", "scr_showpause", "1", yes_no),
    DD(SettingsTab::Game, "HUD", "Ping Graph", "scr_lag_draw", "0", yes_no),
    DD(SettingsTab::Game, "HUD", "Demo Bar", "scr_demobar", "1", demobar_opts),
    SL(SettingsTab::Game, "HUD", "HUD Opacity", "scr_alpha", "1", 0, 1, .05f, 2),
    DD(SettingsTab::Game, "HUD", "HUD Scale", "scr_scale", "0", scale_opts),
    SL(SettingsTab::Game, "HUD", "RmlUi HUD Scale", "ui_rml_hud_scale", "3.0", 0.5f, 6.0f, 0.1f, 1),

    SEC(SettingsTab::Game, "Crosshair"),
    DD(SettingsTab::Game, "Crosshair", "Crosshair Renderer", "ui_crosshair_mode", "1", crosshair_mode_opts),
    DD(SettingsTab::Game, "Crosshair", "New Crosshair Style", "ui_crosshair_style", "0", rml_crosshair_style_opts),
    DD(SettingsTab::Game, "Crosshair", "New Dynamic Crosshair", "ui_crosshair_dynamic", "0", yes_no),
    DD(SettingsTab::Game, "Crosshair", "New Center Dot", "ui_crosshair_dot", "1", yes_no),
    SL(SettingsTab::Game, "Crosshair", "New Outline", "ui_crosshair_outline", "0", 0, 3, 1, 0),
    DD(SettingsTab::Game, "Crosshair", "New T Style", "ui_crosshair_tstyle", "0", yes_no),
    SL(SettingsTab::Game, "Crosshair", "New Size", "ui_crosshair_size", "5", 1, 20, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Thickness", "ui_crosshair_thickness", "1", 1, 5, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Gap", "ui_crosshair_gap", "3", 0, 15, 1, 0),
    SL(SettingsTab::Game, "Crosshair", "New Opacity", "ui_crosshair_opacity", "0.85", 0, 1, .05f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Red", "ui_crosshair_red", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Green", "ui_crosshair_green", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Crosshair", "New Blue", "ui_crosshair_blue", "1", 0, 1, .1f, 2),

    SEC(SettingsTab::Game, "Legacy Crosshair"),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Crosshair Type", "crosshair", "1", crosshair_opts),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Crosshair Scale", "ch_scale", "1", crosshair_scale_opts),
    DD(SettingsTab::Game, "Legacy Crosshair", "Legacy Health Color", "ch_health", "0", yes_no),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Red", "ch_red", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Green", "ch_green", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Blue", "ch_blue", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Alpha", "ch_alpha", "1", 0, 1, .1f, 2),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy X Offset", "ch_x", "0", -100, 100, 1, 0),
    SL(SettingsTab::Game, "Legacy Crosshair", "Legacy Y Offset", "ch_y", "0", -100, 100, 1, 0),

    SEC(SettingsTab::Video, "Display"),
    DD(SettingsTab::Video, "Display", "Renderer", "vid_rtx", "1", renderer_opts),
    DD(SettingsTab::Video, "Display", "Fullscreen Mode", "vid_fullscreen", "0", fullscreen_opts),
    DD(SettingsTab::Video, "Display", "Fullscreen Display", "vid_display", "0", yes_no),
    DD(SettingsTab::Video, "Display", "Vertical Sync", "vid_vsync", "0", yes_no),
    DD(SettingsTab::Video, "Display", "NVIDIA Reflex", "cl_reflex", "2", reflex_opts),
    DD(SettingsTab::Video, "Display", "FPS Counter", "scr_fps", "0", fps_opts),
    SL(SettingsTab::Video, "Display", "Field of View", "fov", "90", 60, 160, 5, 0),
    DD(SettingsTab::Video, "Display", "FOV Scaling", "cl_adjustfov", "1", adjust_fov_opts),
    SL(SettingsTab::Video, "Rendering", "Fixed Resolution Scale", "viewsize", "100", 25, 200, 5, 0),
    DD(SettingsTab::Video, "Rendering", "Dynamic Resolution Scaling", "drs_enable", "0", yes_no),
    SL(SettingsTab::Video, "Rendering", "DRS Target FPS", "drs_target", "60", 30, 250, 1, 0),
    SL(SettingsTab::Video, "Rendering", "DRS Min Scale", "drs_minscale", "50", 25, 100, 5, 0),
    SL(SettingsTab::Video, "Rendering", "DRS Max Scale", "drs_maxscale", "100", 50, 150, 5, 0),
    TOG(SettingsTab::Video, "HDR", "HDR Output", "vid_hdr", "0"),
    SL(SettingsTab::Video, "HDR", "Peak Brightness", "tm_hdr_peak_nits", "1000", 100, 2000, 10, 0),
    SL(SettingsTab::Video, "HDR", "UI Brightness", "ui_hdr_nits", "300", 100, 2000, 10, 0),
    SL(SettingsTab::Video, "HDR", "Saturation", "tm_hdr_saturation_scale", "100", 0, 200, 5, 0),

    SEC(SettingsTab::RTX, "Path Tracer"),
    DD(SettingsTab::RTX, "Path Tracer", "DLSS Super Resolution", "pt_dlss", "1", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "DLSS Quality Mode", "pt_dlss_quality", "0", dlss_quality_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Global Illumination", "pt_num_bounce_rays", "1", gi_opts),
    DD(SettingsTab::RTX, "Path Tracer", "ReSTIR Direct Lighting", "pt_restir_di", "0", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "Reflection / Refraction Depth", "pt_reflect_refract", "1", reflect_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Security Cameras", "pt_cameras", "0", yes_no),
    DD(SettingsTab::RTX, "Path Tracer", "Caustics", "pt_caustics", "1", yes_no),
    SL(SettingsTab::RTX, "Path Tracer", "Texture LOD Bias", "pt_texture_lod_bias", "0", -2, 2, .5f, 1),
    DD(SettingsTab::RTX, "Path Tracer", "Texture Filtering", "pt_nearest", "0", filtering_opts),
    DD(SettingsTab::RTX, "Path Tracer", "Thick Glass Refraction", "pt_thick_glass", "2", thick_glass_opts),
    DD(SettingsTab::RTX, "Upscaling", "Temporal AA", "flt_taa", "1", taa_opts),
    TOG(SettingsTab::RTX, "Upscaling", "FSR Enabled", "flt_fsr_enable", "0"),
    SL(SettingsTab::RTX, "Upscaling", "FSR Sharpness", "flt_fsr_sharpness", "0.2", 0, 2, .05f, 2),
    DD(SettingsTab::RTX, "Denoiser", "Denoiser", "flt_enable", "1", yes_no),
    SL(SettingsTab::RTX, "Tone Mapping", "Exposure Bias", "tm_exposure_bias", "0", -5, 0, .1f, 1),
    SL(SettingsTab::RTX, "Tone Mapping", "Contrast", "tm_reinhard", "0", 0, 1, .1f, 1),
    DD(SettingsTab::RTX, "Tone Mapping", "Bloom", "bloom_enable", "1", yes_no),
    DD(SettingsTab::RTX, "Depth of Field", "Depth of Field", "pt_dof", "1", yes_no),
    DD(SettingsTab::RTX, "Depth of Field", "Aperture Shape", "pt_aperture_type", "0", aperture_opts),
    SL(SettingsTab::RTX, "Depth of Field", "Aperture Rotation", "pt_aperture_angle", "0", 0, 1, .1f, 1),
    DD(SettingsTab::RTX, "Projection", "Projection", "pt_projection", "0", projection_opts),
    DD(SettingsTab::RTX, "Photo Mode", "Accumulation Rendering", "pt_accumulation_rendering", "1", yes_no),
    SL(SettingsTab::RTX, "Photo Mode", "Frames to Accumulate", "pt_accumulation_rendering_framenum", "500", 100, 8000, 100, 0),
    DD(SettingsTab::RTX, "Photo Mode", "Free Camera", "pt_freecam", "1", yes_no),
    DD(SettingsTab::RTX, "Advanced RTX", "God Rays", "gr_enable", "1", yes_no),
    SL(SettingsTab::RTX, "Advanced RTX", "God Ray Intensity", "gr_intensity", "2.0", 0, 10, .1f, 1),
    SL(SettingsTab::RTX, "Advanced RTX", "God Ray Eccentricity", "gr_eccentricity", "0.75", -1, 1, .05f, 2),

    SEC(SettingsTab::Environment, "Sky"),
    DD(SettingsTab::Environment, "Sky", "Sky Type", "physical_sky", "2", sky_opts),
    SL(SettingsTab::Environment, "Sky", "Sun and Sky Brightness", "physical_sky_brightness", "0", -10, 2, .1f, 1),
    DD(SettingsTab::Environment, "Sky", "Time of Day", "sun_preset", "0", sun_preset_opts),
    KEY(SettingsTab::Environment, "Sky", "Next Time of Day", "next_sun"),
    DD(SettingsTab::Environment, "Sky", "Control Sun with Gamepad", "sun_gamepad", "0", sun_gamepad_opts),
    DD(SettingsTab::Environment, "Sky", "Clouds", "physical_sky_draw_clouds", "1", yes_no),
    SL(SettingsTab::Environment, "Custom Sun", "Sun Elevation", "sun_elevation", "45", -20, 90, 1, 0),
    SL(SettingsTab::Environment, "Custom Sun", "Sun Azimuth", "sun_azimuth", "345", -20, 380, 5, 0),
    SL(SettingsTab::Environment, "Custom Sun", "Sun Brightness", "sun_brightness", "10", 0, 50, .5f, 1),
    SL(SettingsTab::Environment, "Custom Sun", "Latitude", "sun_latitude", "32.9", -90, 90, 2, 1),
    DD(SettingsTab::Environment, "Water", "Screen Warping", "pt_waterwarp", "0", yes_no),
    DD(SettingsTab::Environment, "Water", "Water Mode", "pt_water_fbm", "0", water_opts),
    SL(SettingsTab::Environment, "Water", "Water Wave Scale", "pt_water_wave_scale", "1", .1f, 4, .1f, 1),
    SL(SettingsTab::Environment, "Water", "Water Wave Speed", "pt_water_wave_speed", "1", 0, 4, .1f, 1),
    SL(SettingsTab::Environment, "Water", "Water Wave Steepness", "pt_water_wave_steepness", "1", 1, 4, .1f, 1),

    SEC(SettingsTab::Effects, "Particles & Explosions"),
    SL(SettingsTab::Effects, "Particles & Explosions", "Particle Multiplier", "cl_particle_num_factor", "1", 0, 4, .1f, 1),
    SL(SettingsTab::Effects, "Particles & Explosions", "Particle Emissive", "pt_particle_emissive", "10.0", 0, 50, .5f, 1),
    TOG(SettingsTab::Effects, "Particles & Explosions", "Explosion Sprites", "cl_explosion_sprites", "1"),
    SL(SettingsTab::Effects, "Particles & Explosions", "Explosion Frametime", "cl_explosion_frametime", "20", 10, 100, 1, 0),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Grenade Explosions", "cl_disable_explosions", "0", 0, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Rocket Explosions", "cl_disable_explosions", "0", 1, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Grenade Particles", "cl_disable_particles", "0", 0, true),
    BIT(SettingsTab::Effects, "Particles & Explosions", "Rocket Trails", "cl_disable_particles", "0", 3, true),
    ITOG(SettingsTab::Effects, "Entity FX", "Glow on Items", "cl_noglow", "0"),
    ITOG(SettingsTab::Effects, "Entity FX", "Item Bobbing", "cl_nobob", "0"),
    TOG(SettingsTab::Effects, "Screen Effects", "Screen Blending", "tm_blend_enable", "1"),
    SL(SettingsTab::Effects, "Screen Effects", "Blend Center Strength", "tm_blend_scale_center", "0", 0, 1, .05f, 2),
    SL(SettingsTab::Effects, "Screen Effects", "Blend Border Strength", "tm_blend_scale_border", "0", 0, 1, .05f, 2),
    DD(SettingsTab::Effects, "Rail Gun", "Rail Trail Type", "cl_railtrail_type", "0", rail_type_opts),
    SL(SettingsTab::Effects, "Rail Gun", "Rail Trail Duration", "cl_railtrail_time", "1.0", .1f, 3, .1f, 1),
    SL(SettingsTab::Effects, "Rail Gun", "Core Width", "cl_railcore_width", "2", 1, 6, 1, 0),
    SL(SettingsTab::Effects, "Rail Gun", "Spiral Radius", "cl_railspiral_radius", "3", 1, 6, 1, 0),
    DD(SettingsTab::Effects, "Rail Gun", "Core Color", "cl_railcore_color", "red", color_opts),
    DD(SettingsTab::Effects, "Rail Gun", "Spiral Color", "cl_railspiral_color", "blue", color_opts),

    SEC(SettingsTab::Developer, "RTX Diagnostics"),
    DD(SettingsTab::Developer, "RTX Diagnostics", "ReSTIR DI Debug View", "pt_restir_di_debug", "0", restir_debug_opts),
    TOG(SettingsTab::Developer, "RTX Diagnostics", "ReSTIR DI Spatial Reuse", "pt_restir_di_spatial", "1"),
    DD(SettingsTab::Developer, "RTX Diagnostics", "ReSTIR Denoiser", "pt_nrd", "1", nrd_opts),
    TOG(SettingsTab::Developer, "RTX Diagnostics", "Light PDF Correction", "pt_light_stats", "0"),
    TOG(SettingsTab::Developer, "RTX Diagnostics", "Show Sky Geometry", "pt_show_sky", "0"),
    TOG(SettingsTab::Developer, "Frame Debug", "GPU Profiler", "profiler", "0"),
    DD(SettingsTab::Developer, "Frame Debug", "Ray Tracing API", "ray_tracing_api", "auto", rt_api_opts),
    DD(SettingsTab::Developer, "Frame Debug", "Tone Mapping Debug", "tm_debug", "0", tm_debug_opts),
    TOG(SettingsTab::Developer, "Frame Debug", "SVGF Gradient Overlay", "flt_show_gradients", "0"),
    BIT(SettingsTab::Developer, "Frame Debug", "Fixed Albedo Debug", "flt_fixed_albedo", "0", 1, true),
    DD(SettingsTab::Developer, "Frame Debug", "Multi-GPU Support", "sli", "1", sli_opts),
    TOG_DESC(SettingsTab::Developer, "NVIDIA Debug", "NVIDIA Streamline Debug Overlay", "r_streamline_imgui", "0",
             "Development builds only. Requires restart. Shows NVIDIA Streamline plugin debug panels for Reflex, DLSS, DLSS-G, and visual stats when available."),

    SEC(SettingsTab::Audio, "Volume"),
    SL(SettingsTab::Audio, "Volume", "Master Volume", "s_volume", "1", 0, 1, .05f, 2),
    SL(SettingsTab::Audio, "Volume", "Music Volume", "ogg_volume", "1", 0, 1, .05f, 2),
    TOG(SettingsTab::Audio, "Volume", "Music Enabled", "ogg_enable", "1"),
    TOG(SettingsTab::Audio, "Volume", "Title Menu Track", "ui_title_music", "1"),
    TOG(SettingsTab::Audio, "Volume", "Shuffle Tracks", "ogg_shuffle", "0"),
    TOG(SettingsTab::Audio, "Sound Engine", "Underwater Effect", "s_underwater", "1"),
    DD(SettingsTab::Audio, "Sound Engine", "Ambient Sounds", "s_ambient", "1", ambient_opts),
    DD(SettingsTab::Audio, "Sound Engine", "Chat Beep", "cl_chat_sound", "1", chat_sound_opts),
    DD(SettingsTab::Audio, "Sound Engine", "Sound Engine", "s_enable", "2", sound_engine_opts),
    TXT(SettingsTab::Audio, "Sound Engine", "OpenAL Device", "al_device", ""),
    SL(SettingsTab::Audio, "Sound Engine", "Mix Ahead", "s_mixahead", "0.2", .01f, .5f, .01f, 2),

    SEC(SettingsTab::Controls, "Mouse"),
    SL(SettingsTab::Controls, "Mouse", "Mouse Sensitivity", "sensitivity", "3", 1, 30, .1f, 1),
    DD(SettingsTab::Controls, "Mouse", "Invert Mouse", "m_invert", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Adjust Sensitivity with FOV", "m_autosens", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Filter Mouse Input", "m_filter", "0", yes_no),
    DD(SettingsTab::Controls, "Mouse", "Free Look", "freelook", "1", yes_no),
    DD(SettingsTab::Controls, "Movement", "Always Run", "cl_run", "1", yes_no),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Attack", "+attack"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Next Weapon", "weapnext"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Previous Weapon", "weapprev"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Walk Forward", "+forward"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Backpedal", "+back"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Run", "+speed"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Step Left", "+moveleft"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Step Right", "+moveright"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Jump", "+moveup"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Crouch", "+movedown"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Inventory", "inven"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Use Item", "invuse"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Drop Item", "invdrop"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Previous Item", "invprev"),
    KEY(SettingsTab::Controls, "Action Key Bindings", "Next Item", "invnext"),
    KEY(SettingsTab::Controls, "Function Keys", "Screenshot", "screenshot"),
    KEY(SettingsTab::Controls, "Function Keys", "Pause Game", "pause"),
    KEY(SettingsTab::Controls, "Function Keys", "Score Table", "score"),
    KEY(SettingsTab::Controls, "Function Keys", "Chat", "messagemode"),
};

const char *tab_names[] = {
    "Gameplay",
    "Graphics",
    "Ray Tracing",
    "Environment",
    "Effects",
    "Developer",
    "Audio",
    "Controls",
    "Crosshair"
};

float clampf(float v, float mn, float mx)
{
    return std::max(mn, std::min(v, mx));
}

std::string format_float(float value, int decimals)
{
    char buffer[64];
    if (decimals <= 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    } else if (decimals == 1) {
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    } else if (decimals == 2) {
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    }
    return buffer;
}

std::string utf8_from_codepoint(unsigned cp)
{
    std::string s;
    if (cp <= 0x7Fu) {
        s.push_back((char)cp);
    } else if (cp <= 0x7FFu) {
        s.push_back((char)(0xC0u | (cp >> 6)));
        s.push_back((char)(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        s.push_back((char)(0xE0u | (cp >> 12)));
        s.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        s.push_back((char)(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        s.push_back((char)(0xF0u | (cp >> 18)));
        s.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        s.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        s.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    return s;
}

std::vector<std::string> tokenize_menu_list(const char *text)
{
    std::vector<std::string> tokens;
    if (!text) {
        return tokens;
    }

    const char *p = text;
    while (*p) {
        while (*p && (unsigned char)*p <= ' ') {
            ++p;
        }
        if (!*p) {
            break;
        }

        std::string token;
        if (*p == '"') {
            ++p;
            while (*p && *p != '"') {
                token.push_back(*p++);
            }
            if (*p == '"') {
                ++p;
            }
        } else {
            while (*p && (unsigned char)*p > ' ') {
                token.push_back(*p++);
            }
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string display_label_for_mode(const std::string& raw)
{
    if (raw == "desktop") {
        return "Desktop resolution";
    }
    return raw;
}

std::vector<Choice> choices_for_def(const Def& def)
{
    std::vector<Choice> choices;
    if (def.cvar && !std::strcmp(def.cvar, "vid_fullscreen")) {
        choices.push_back({ "Windowed", "0" });
        cvar_t *modelist = Cvar_WeakGet("vid_modelist");
        std::vector<std::string> modes = tokenize_menu_list(modelist ? modelist->string : nullptr);
        for (int i = 0; i < (int)modes.size(); ++i) {
            char value[16];
            std::snprintf(value, sizeof(value), "%d", i + 1);
            choices.push_back({ display_label_for_mode(modes[i]), value });
        }
        return choices;
    }

    if (def.cvar && !std::strcmp(def.cvar, "vid_display")) {
        cvar_t *displaylist = Cvar_WeakGet("vid_displaylist");
        std::vector<std::string> tokens = tokenize_menu_list(displaylist ? displaylist->string : nullptr);
        for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
            choices.push_back({ tokens[i], tokens[i + 1] });
        }
        if (choices.empty()) {
            choices.push_back({ "Display 1", "0" });
        }
        return choices;
    }

    if (def.options) {
        for (int i = 0; i < def.option_count; ++i) {
            choices.push_back({ def.options[i].label ? def.options[i].label : "", def.options[i].value ? def.options[i].value : "" });
        }
    }
    return choices;
}

const char *section_name_for_def(const Def& def)
{
    if (def.kind == Kind::Section) {
        return (def.label && def.label[0]) ? def.label : "General";
    }
    return (def.section && def.section[0]) ? def.section : "General";
}

std::vector<std::string> section_names_for_tab(SettingsTab tab)
{
    std::vector<std::string> names;
    for (const Def& def : vanilla_settings_defs) {
        if (def.tab != tab) {
            continue;
        }
        const char *name = section_name_for_def(def);
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.emplace_back(name);
        }
    }
    return names;
}

bool should_create_missing_cvar(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }

    return !std::strcmp(name, "crosshair") ||
           !std::strcmp(name, "ui_crosshair_mode") ||
           !std::strncmp(name, "ch_", 3) ||
           !std::strncmp(name, "ui_crosshair_", 13);
}

cvar_t *setting_cvar(const Def& def)
{
    if (!def.cvar || !def.cvar[0]) {
        return nullptr;
    }

    cvar_t *cv = Cvar_WeakGet(def.cvar);
    if (!cv && should_create_missing_cvar(def.cvar)) {
        cv = Cvar_Get(def.cvar, def.default_value ? def.default_value : "", CVAR_ARCHIVE);
    }
    return cv;
}

const char *kind_badge(Kind kind)
{
    switch (kind) {
    case Kind::Toggle:
    case Kind::Bitmask:
        return "Switch";
    case Kind::Slider:
        return "Slider";
    case Kind::Dropdown:
        return "Choice";
    case Kind::Text:
        return "Text";
    case Kind::Keybind:
        return "Key";
    case Kind::Link:
        return "Action";
    case Kind::Section:
        break;
    }
    return "";
}

const char *kind_name(Kind kind)
{
    switch (kind) {
    case Kind::Section: return "section";
    case Kind::Toggle: return "toggle";
    case Kind::Slider: return "slider";
    case Kind::Dropdown: return "dropdown";
    case Kind::Text: return "text";
    case Kind::Bitmask: return "bitmask";
    case Kind::Keybind: return "keybind";
    case Kind::Link: return "link";
    }
    return "unknown";
}

void settings_debug(const char *fmt, ...)
{
    if (!ui_rmlui_debug || !ui_rmlui_debug->integer) {
        return;
    }

    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Com_LPrintf(PRINT_DEVELOPER, "%s", buffer);
}

Rml::Element *find_ancestor_with_attribute(Rml::Element *element, const char *attribute)
{
    while (element) {
        if (element->HasAttribute(attribute)) {
            return element;
        }
        element = element->GetParentNode();
    }
    return nullptr;
}

Rml::Element *event_element_with_attribute(Rml::Event& event, const char *attribute)
{
    if (Rml::Element *element = find_ancestor_with_attribute(event.GetTargetElement(), attribute)) {
        return element;
    }
    return find_ancestor_with_attribute(event.GetCurrentElement(), attribute);
}

} // namespace

bool SettingsMenu::Init(Rml::Context *ctx)
{
    if (!ctx) return false;
    Shutdown();
    m_ctx = ctx;
    m_doc = ctx->LoadDocument("ui/settings/settings.rml");
    if (!m_doc) {
        m_ctx = nullptr;
        return false;
    }
    m_doc->Show();
    m_root = m_doc->GetElementById("settings-root");
    m_shell = m_doc->GetElementById("settings-shell");
    m_home = m_doc->GetElementById("settings-home");
    m_panel = m_doc->GetElementById("settings-panel");
    m_content = m_doc->GetElementById("settings-content");
    m_breadcrumb = m_doc->GetElementById("settings-breadcrumb");
    m_title = m_doc->GetElementById("settings-title");
    m_dirty = m_doc->GetElementById("settings-dirty");
    m_confirm = m_doc->GetElementById("settings-confirm");
    m_reset = m_doc->GetElementById("btn-reset");
    m_back = m_doc->GetElementById("btn-back");
    if (m_root) {
        m_root->AddEventListener(Rml::EventId::Click, this);
        m_root->AddEventListener(Rml::EventId::Mouseover, this);
        m_root->AddEventListener(Rml::EventId::Mousedown, this);
        m_root->AddEventListener(Rml::EventId::Mousemove, this);
        m_root->AddEventListener(Rml::EventId::Mouseup, this);
    }
    const char *ids[] = {
        "tree-game",
        "tree-video",
        "tree-rtx",
        "tree-environment",
        "tree-effects",
        "tree-developer",
        "tree-audio",
        "tree-controls",
        "tree-crosshair"
    };
    for (int i = 0; i < (int)SettingsTab::COUNT; ++i) {
        m_tab_btns[i] = m_doc->GetElementById(ids[i]);
        if (m_tab_btns[i]) {
            m_tab_btns[i]->AddEventListener(Rml::EventId::Click, this);
            m_tab_btns[i]->AddEventListener(Rml::EventId::Mousedown, this);
        }
    }
    Rml::ElementList actions;
    m_doc->QuerySelectorAll(actions, "[data-settings-action]");
    for (Rml::Element *element : actions) {
        element->AddEventListener(Rml::EventId::Click, this);
    }
    BuildTabContent(m_active_tab);
    Hide();
    return m_shell && m_home && m_panel && m_content;
}

void SettingsMenu::Show(SettingsTab tab)
{
    m_visible = true;
    m_showing_root = false;
    m_showing_section_menu = true;
    if (m_root) {
        m_root->SetAttribute("class", m_transparent_background ? "pause-root" : "menu-root");
    }
    if (m_shell) {
        m_shell->SetClass("settings-shell--home", false);
        m_shell->SetClass("settings-shell--section", true);
        m_shell->SetClass("settings-shell--controls", false);
        m_shell->SetClass("settings-shell--panel", false);
    }
    SetDisplay(m_root, true);
    SetDisplay(m_home, false);
    SetDisplay(m_panel, true);
    SetDisplay(m_reset, false);
    SetText(m_back, "Back");
    SwitchTab(tab);
    SyncAllFromCvars();
}

void SettingsMenu::ShowSection(SettingsTab tab, const char *section)
{
    Show(tab);
    BuildSectionContent(tab, section && section[0] ? section : "Player");
}

void SettingsMenu::ShowRoot()
{
    m_visible = true;
    m_showing_root = true;
    m_showing_section_menu = false;
    m_active_section.clear();
    CancelRebind();
    FinishTextEdit(false);
    if (m_root) {
        m_root->SetAttribute("class", m_transparent_background ? "pause-root" : "menu-root");
    }
    if (m_shell) {
        m_shell->SetClass("settings-shell--home", true);
        m_shell->SetClass("settings-shell--section", false);
        m_shell->SetClass("settings-shell--controls", false);
        m_shell->SetClass("settings-shell--panel", false);
    }
    SetDisplay(m_root, true);
    SetDisplay(m_home, true);
    SetDisplay(m_panel, false);
    SetDisplay(m_reset, false);
    SetText(m_back, "Close");
    SetText(m_title, "Settings");
    SetText(m_breadcrumb, "Settings");
    if (m_content) {
        m_content->SetInnerRML("");
    }
    m_bindings.clear();
    m_section_btns.clear();
    m_section_names.clear();
    m_focus_index = -1;
    m_drag_slider_index = -1;
    m_text_cursor = -1;
    SyncDirtyState();
}

void SettingsMenu::Hide()
{
    m_visible = false;
    CancelRebind();
    FinishTextEdit(false);
    if (m_drag_slider_index >= 0 && m_drag_slider_index < (int)m_bindings.size()) {
        if (m_bindings[m_drag_slider_index].slider_wrap) {
            m_bindings[m_drag_slider_index].slider_wrap->SetClass("dragging", false);
        }
        if (m_bindings[m_drag_slider_index].row) {
            m_bindings[m_drag_slider_index].row->SetClass("active", false);
        }
    }
    m_drag_slider_index = -1;
    SetDisplay(m_root, false);
    SetDisplay(m_confirm, false);
    FlushConfig();
}

void SettingsMenu::SetTransparentBackground(bool transparent)
{
    m_transparent_background = transparent;
    if (m_root) {
        m_root->SetAttribute("class", transparent ? "pause-root" : "menu-root");
    }
}

void SettingsMenu::Toggle()
{
    if (m_visible) RequestBack(); else ShowRoot();
}

bool SettingsMenu::TakeCloseRequest()
{
    bool result = m_close_requested;
    m_close_requested = false;
    return result;
}

void SettingsMenu::Shutdown()
{
    if (m_doc) m_doc->Close();
    m_ctx = nullptr;
    m_doc = nullptr;
    m_root = nullptr;
    m_shell = nullptr;
    m_home = nullptr;
    m_panel = nullptr;
    m_content = nullptr;
    m_breadcrumb = nullptr;
    m_title = nullptr;
    m_dirty = nullptr;
    m_confirm = nullptr;
    m_reset = nullptr;
    m_back = nullptr;
    m_showing_root = true;
    m_showing_section_menu = false;
    m_visible = false;
    m_transparent_background = false;
    m_dirty_state = false;
    m_needs_config_write = false;
    m_bindings.clear();
    m_section_btns.clear();
    m_section_names.clear();
    m_active_section.clear();
    m_focus_index = -1;
    m_rebind_index = -1;
    m_text_index = -1;
    m_drag_slider_index = -1;
    m_text_cursor = -1;
    m_text_original.clear();
    for (Rml::Element *&element : m_tab_btns) element = nullptr;
}

void SettingsMenu::SwitchTab(SettingsTab tab)
{
    if (m_visible && tab != m_active_tab) {
        UI_StartSound(QMS_MOVE);
    }
    m_showing_root = false;
    m_showing_section_menu = true;
    m_active_section.clear();
    m_active_tab = tab;
    if (m_shell) {
        m_shell->SetClass("settings-shell--home", false);
        m_shell->SetClass("settings-shell--section", true);
        m_shell->SetClass("settings-shell--controls", false);
        m_shell->SetClass("settings-shell--panel", false);
    }
    SetDisplay(m_home, false);
    SetDisplay(m_panel, true);
    SetDisplay(m_reset, false);
    SetText(m_back, "Back");
    SetText(m_title, tab_names[(int)tab]);
    std::string breadcrumb = std::string("Settings / ") + tab_names[(int)tab];
    SetText(m_breadcrumb, breadcrumb.c_str());
    BuildSectionMenu(tab);
    SyncAllFromCvars();
}

void SettingsMenu::BuildTabContent(SettingsTab tab)
{
    BuildSectionMenu(tab);
}

void SettingsMenu::BuildSectionMenu(SettingsTab tab)
{
    if (!m_content) return;
    m_content->SetInnerRML("");
    m_content->SetScrollTop(0.0f);
    m_bindings.clear();
    m_section_btns.clear();
    m_section_names = section_names_for_tab(tab);
    m_focus_index = -1;
    m_drag_slider_index = -1;
    m_text_index = -1;
    m_text_cursor = -1;
    m_text_original.clear();

    m_showing_root = false;
    m_showing_section_menu = true;
    m_active_tab = tab;
    m_active_section.clear();
    if (m_shell) {
        m_shell->SetClass("settings-shell--home", false);
        m_shell->SetClass("settings-shell--section", true);
        m_shell->SetClass("settings-shell--controls", false);
        m_shell->SetClass("settings-shell--panel", false);
    }
    SetDisplay(m_reset, false);
    SetText(m_title, tab_names[(int)tab]);
    std::string breadcrumb = std::string("Settings / ") + tab_names[(int)tab];
    SetText(m_breadcrumb, breadcrumb.c_str());

    if (m_section_names.empty()) {
        m_section_names.emplace_back("General");
    }

    AddSection(m_content, tab_names[(int)tab]);
    for (int i = 0; i < (int)m_section_names.size(); ++i) {
        Rml::Element *button = AppendTextElement(m_content, "button", "tree-item section-item", m_section_names[i].c_str());
        m_section_btns.push_back(button);
        if (!button) {
            continue;
        }
        button->SetAttribute("data-settings-section", i);
        button->AddEventListener(Rml::EventId::Click, this);
        button->AddEventListener(Rml::EventId::Mousedown, this);
    }
}

void SettingsMenu::BuildSectionContent(SettingsTab tab, const std::string& section)
{
    if (!m_content) return;
    m_content->SetInnerRML("");
    m_content->SetScrollTop(0.0f);
    m_bindings.clear();
    m_section_btns.clear();
    m_focus_index = -1;
    m_drag_slider_index = -1;
    m_text_index = -1;
    m_text_cursor = -1;
    m_text_original.clear();

    m_showing_root = false;
    m_showing_section_menu = false;
    m_active_tab = tab;
    m_active_section = section;
    if (m_shell) {
        m_shell->SetClass("settings-shell--home", false);
        m_shell->SetClass("settings-shell--section", false);
        m_shell->SetClass("settings-shell--controls", true);
        m_shell->SetClass("settings-shell--panel", false);
    }
    SetDisplay(m_reset, true);
    SetText(m_reset, "Reset Section");
    SetText(m_title, section.c_str());
    std::string breadcrumb = std::string("Settings / ") + tab_names[(int)tab] + " / " + section;
    SetText(m_breadcrumb, breadcrumb.c_str());

    AddSection(m_content, section.c_str());
    for (const Def& def : vanilla_settings_defs) {
        if (def.tab != tab || def.kind == Kind::Section) {
            continue;
        }
        if (section != section_name_for_def(def)) {
            continue;
        }
        AddSetting(m_content, def);
    }
    if (m_bindings.empty()) {
        AppendTextElement(m_content, "div", "settings-empty", "No settings available");
    }
    if (!m_bindings.empty()) {
        FocusBinding(0, true);
    }
    SyncAllFromCvars();
}

void SettingsMenu::SwitchSection(const std::string& section)
{
    if (section.empty()) {
        return;
    }
    UI_StartSound(QMS_MOVE);
    BuildSectionContent(m_active_tab, section);
}

Rml::Element *SettingsMenu::AddSection(Rml::Element *parent, const char *label)
{
    return AppendTextElement(parent, "div", "settings-section", label);
}

SettingsMenu::Binding& SettingsMenu::AddSetting(Rml::Element *parent, const SettingDef& def)
{
    const int binding_index = (int)m_bindings.size();
    m_bindings.push_back({});
    Binding& binding = m_bindings.back();
    binding.def = &def;
    binding.available = !def.cvar || def.kind == Kind::Keybind || def.kind == Kind::Link || setting_cvar(def) != nullptr;
    binding.pending = CvarString(def);
    binding.row = AppendElement(parent, "div", "settings-row setting-row");
    binding.row->SetClass("setting-row--toggle", def.kind == Kind::Toggle || def.kind == Kind::Bitmask);
    binding.row->SetClass("setting-row--slider", def.kind == Kind::Slider);
    binding.row->SetClass("setting-row--choice", def.kind == Kind::Dropdown || def.kind == Kind::Text || def.kind == Kind::Keybind);
    binding.row->SetClass("setting-row--link", def.kind == Kind::Link);
    binding.row->SetClass("setting-row--unavailable", !binding.available);
    binding.row->SetClass("settings-row--unavailable", !binding.available);
    binding.row->SetAttribute("data-settings-bind", binding_index);
    binding.row->SetAttribute("data-row-id", binding_index);
    binding.row->SetAttribute("data-type", kind_name(def.kind));
    if (def.cvar && def.cvar[0]) {
        binding.row->SetAttribute("data-cvar", def.cvar);
    }
    binding.row->AddEventListener(Rml::EventId::Click, this);
    binding.row->AddEventListener(Rml::EventId::Mouseover, this);

    Rml::Element *main = AppendElement(binding.row, "div", "row-main");
    Rml::Element *info = AppendElement(main, "div", "setting-info");
    binding.label = AppendTextElement(info, "div", "row-label setting-label", def.label);
    const char *description = (def.description && def.description[0]) ? def.description : nullptr;
    if (description) {
        binding.description = AppendTextElement(info, "div", "row-description setting-description", description);
    }
    AppendTextElement(info, "div", "control-kind", binding.available ? kind_badge(def.kind) : "Unavailable in this build");
    if (def.cvar && !std::strcmp(def.cvar, "r_streamline_imgui")) {
        AppendTextElement(info, "div", "restart-badge", "Restart required");
    }

    Rml::Element *control = AppendElement(main, "div", "row-control setting-control");
    binding.control = control;
    binding.value = AppendTextElement(binding.row, "div", "row-value setting-value", "");

    if (def.kind == Kind::Toggle || def.kind == Kind::Bitmask) {
        Rml::Element *switch_control = AppendElement(control, "div", "switch-control");
        AppendTextElement(switch_control, "div", "switch-option switch-option--off", "OFF");
        AppendTextElement(switch_control, "div", "switch-option switch-option--on", "ON");
        binding.control = switch_control;
    } else if (def.kind == Kind::Slider) {
        Rml::Element *minus = AppendTextElement(control, "button", "step-btn", "-");
        minus->SetAttribute("data-settings-step", "-1");
        minus->SetAttribute("data-settings-bind", binding_index);
        minus->AddEventListener(Rml::EventId::Click, this);
        Rml::Element *wrap = AppendElement(control, "div", "slider-wrap setting-slider-wrap");
        binding.slider_wrap = wrap;
        wrap->SetAttribute("data-settings-bind", binding_index);
        wrap->AddEventListener(Rml::EventId::Click, this);
        wrap->AddEventListener(Rml::EventId::Mousedown, this);
        wrap->AddEventListener(Rml::EventId::Mousemove, this);
        wrap->AddEventListener(Rml::EventId::Mouseup, this);
        wrap->AddEventListener(Rml::EventId::Mouseout, this);
        AppendElement(wrap, "div", "slider-track setting-slider-track");
        binding.fill = AppendElement(wrap, "div", "slider-fill setting-slider-fill");
        binding.thumb = AppendElement(wrap, "div", "slider-thumb setting-slider-thumb");
        binding.display = AppendTextElement(wrap, "div", "slider-readout", "");
        Rml::Element *plus = AppendTextElement(control, "button", "step-btn", "+");
        plus->SetAttribute("data-settings-step", "1");
        plus->SetAttribute("data-settings-bind", binding_index);
        plus->AddEventListener(Rml::EventId::Click, this);
    } else if (def.kind == Kind::Dropdown) {
        Rml::Element *prev = AppendTextElement(control, "button", "choice-btn", "<");
        prev->SetAttribute("data-settings-step", "-1");
        prev->SetAttribute("data-settings-bind", binding_index);
        prev->AddEventListener(Rml::EventId::Click, this);
        Rml::Element *box = AppendElement(control, "div", "select-box");
        binding.control = box;
        binding.display = AppendTextElement(box, "div", "select-label", "");
        AppendTextElement(box, "div", "select-hint", "Choice");
        Rml::Element *next = AppendTextElement(control, "button", "choice-btn", ">");
        next->SetAttribute("data-settings-step", "1");
        next->SetAttribute("data-settings-bind", binding_index);
        next->AddEventListener(Rml::EventId::Click, this);
    } else if (def.kind == Kind::Text) {
        Rml::Element *box = AppendElement(control, "div", "text-box");
        binding.control = box;
        binding.display = AppendTextElement(box, "div", "text-label", "");
        AppendTextElement(box, "div", "text-hint", "Text");
    } else if (def.kind == Kind::Keybind) {
        Rml::Element *box = AppendElement(control, "div", "keybind-pill");
        binding.control = box;
        binding.display = AppendTextElement(box, "div", "keybind-label", "");
        AppendTextElement(box, "div", "keybind-hint", "Bind");
    } else if (def.kind == Kind::Link) {
        binding.control = AppendTextElement(control, "button", nullptr, "Open");
    }
    SyncBinding(binding);
    return binding;
}

std::string SettingsMenu::CvarString(const SettingDef& def) const
{
    if (!def.cvar) return "";
    cvar_t *cv = setting_cvar(def);
    if (cv && cv->latched_string) {
        return cv->latched_string;
    }
    return cv && cv->string ? cv->string : (def.default_value ? def.default_value : "");
}

int SettingsMenu::OptionIndex(const SettingDef& def, const std::string& value) const
{
    std::vector<Choice> choices = choices_for_def(def);
    for (int i = 0; i < (int)choices.size(); ++i) {
        if (value == choices[i].value) return i;
    }
    return 0;
}

std::string SettingsMenu::FormatValue(const SettingDef& def, const std::string& value) const
{
    if (def.kind == Kind::Toggle || def.kind == Kind::Bitmask) {
        const bool on = value != "0";
        return on ? "ON" : "OFF";
    }
    if (def.kind == Kind::Dropdown) {
        std::vector<Choice> choices = choices_for_def(def);
        if (!choices.empty()) {
            return choices[OptionIndex(def, value)].label;
        }
    }
    if (def.kind == Kind::Slider) {
        return format_float((float)std::atof(value.c_str()), def.decimals);
    }
    if (def.kind == Kind::Keybind) {
        int key = Key_EnumBindings(0, def.cvar);
        return key == -1 ? "-" : Key_KeynumToString(key);
    }
    if (def.kind == Kind::Link) {
        return "";
    }
    return value;
}

std::string ParameterText(const Def& def, const std::string& raw_value, const std::string& display_value)
{
    if (def.kind == Kind::Link) {
        return "";
    }
    if (def.kind == Kind::Keybind) {
        return std::string("Command: ") + (def.cvar ? def.cvar : "");
    }
    if (!def.cvar || !def.cvar[0]) {
        return "";
    }
    if (def.kind == Kind::Slider) {
        return std::string(def.cvar) + " = " + display_value +
               " | min " + format_float(def.min_value, def.decimals) +
               " | max " + format_float(def.max_value, def.decimals) +
               " | step " + format_float(def.step, def.decimals);
    }
    if (def.kind == Kind::Text) {
        return std::string(def.cvar) + " = \"" + raw_value + "\"";
    }
    if (def.kind == Kind::Dropdown) {
        return std::string(def.cvar) + " = " + raw_value;
    }
    return std::string(def.cvar) + " = " + display_value + " (" + raw_value + ")";
}

void SettingsMenu::SetPending(Binding& binding, const std::string& value)
{
    const Def& def = *binding.def;
    if (def.kind == Kind::Bitmask) {
        cvar_t *cv = Cvar_WeakGet(def.cvar);
        if (!cv) cv = setting_cvar(def);
        if (!cv) return;
        int raw = cv ? cv->integer : 0;
        bool logical_on = value != "0";
        bool bit_on = def.inverted ? !logical_on : logical_on;
        if (bit_on) raw |= (1 << def.bit); else raw &= ~(1 << def.bit);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d", raw);
        Cvar_SetEx(def.cvar, buffer, FROM_MENU);
        m_needs_config_write = true;
    } else if (def.kind == Kind::Toggle && def.inverted) {
        if (!setting_cvar(def)) return;
        Cvar_SetEx(def.cvar, value == "0" ? "1" : "0", FROM_MENU);
        m_needs_config_write = true;
    } else if (def.cvar && def.kind != Kind::Keybind && def.kind != Kind::Link) {
        if (!setting_cvar(def)) return;
        Cvar_SetEx(def.cvar, value.c_str(), FROM_MENU);
        m_needs_config_write = true;
    }
    SyncAllFromCvars();
}

void SettingsMenu::SyncBinding(Binding& binding)
{
    const Def& def = *binding.def;
    std::string display = FormatValue(def, binding.pending);
    SetText(binding.value, ParameterText(def, binding.pending, display).c_str());
    if (def.kind == Kind::Toggle || def.kind == Kind::Bitmask) {
        SetDisplay(binding.value, true);
        bool on = binding.pending != "0";
        if (binding.control) {
            binding.control->SetClass("switch--on", on);
            binding.control->SetClass("switch--off", !on);
        }
        if (binding.value) {
            binding.value->SetClass("value--on", on);
            binding.value->SetClass("value--off", !on);
        }
    } else if (def.kind == Kind::Slider) {
        float v = (float)std::atof(binding.pending.c_str());
        float pct_value = (def.max_value != def.min_value) ? ((v - def.min_value) / (def.max_value - def.min_value)) * 100.0f : 0.0f;
        pct_value = clampf(pct_value, 0.0f, 100.0f);
        SetDisplay(binding.value, true);
        SetPercent(binding.fill, "width", pct_value);
        SetPercent(binding.thumb, "left", pct_value);
        SetText(binding.display, display.c_str());
    } else if (def.kind == Kind::Dropdown || def.kind == Kind::Text || def.kind == Kind::Keybind) {
        SetDisplay(binding.value, true);
        if (def.kind == Kind::Dropdown) {
            SetText(binding.display, display.empty() ? "(no choices available)" : display.c_str());
        } else if (def.kind == Kind::Text && m_text_index >= 0 && m_text_index < (int)m_bindings.size() &&
                   &m_bindings[m_text_index] == &binding) {
            std::string edit_display = display;
            const int cursor = std::max(0, std::min(m_text_cursor, (int)edit_display.size()));
            edit_display.insert((size_t)cursor, "|");
            SetText(binding.display, edit_display.c_str());
        } else {
            SetText(binding.display, display.c_str());
        }
    } else if (def.kind == Kind::Link) {
        SetDisplay(binding.value, false);
    }
}

void SettingsMenu::SyncAllFromCvars()
{
    for (int idx = 0; idx < (int)m_bindings.size(); ++idx) {
        Binding& binding = m_bindings[idx];
        const Def& def = *binding.def;
        const bool keep_active_text = (m_text_index == idx && def.kind == Kind::Text);
        if (!keep_active_text) {
            if (def.kind == Kind::Bitmask) {
                int raw = std::atoi(CvarString(def).c_str());
                bool bit_on = (raw & (1 << def.bit)) != 0;
                bool logical_on = def.inverted ? !bit_on : bit_on;
                binding.pending = logical_on ? "1" : "0";
            } else if (def.kind != Kind::Keybind && def.kind != Kind::Link) {
                std::string raw = CvarString(def);
                if (def.kind == Kind::Toggle && def.inverted) {
                    raw = raw == "0" ? "1" : "0";
                }
                binding.pending = raw;
            }
        }
        SyncBinding(binding);
    }
    SyncDirtyState();
}

void SettingsMenu::SyncDirtyState()
{
    m_dirty_state = false;
    SetDisplay(m_dirty, m_dirty_state);
}

void SettingsMenu::FlushConfig()
{
    if (!m_needs_config_write) {
        return;
    }
    CL_WriteConfig();
    m_needs_config_write = false;
}

float SettingsMenu::SliderValueFromMouse(const Binding& binding, float mouse_x) const
{
    const Def& def = *binding.def;
    if (!binding.slider_wrap || def.kind != Kind::Slider) {
        return clampf((float)std::atof(binding.pending.c_str()), def.min_value, def.max_value);
    }

    const Rml::Vector2f pos = binding.slider_wrap->GetAbsoluteOffset(Rml::BoxArea::Content);
    const Rml::Vector2f size = binding.slider_wrap->GetBox().GetSize(Rml::BoxArea::Content);
    const float pct = size.x > 1.0f ? clampf((mouse_x - pos.x) / size.x, 0.0f, 1.0f) : 0.0f;
    float value = def.min_value + pct * (def.max_value - def.min_value);
    if (def.step > 0.0f) {
        value = def.min_value + std::round((value - def.min_value) / def.step) * def.step;
    }
    return clampf(value, def.min_value, def.max_value);
}

void SettingsMenu::SetSliderFromMouse(Binding& binding, float mouse_x, const char *source)
{
    const Def& def = *binding.def;
    if (def.kind != Kind::Slider) {
        return;
    }

    int binding_index = -1;
    for (int i = 0; i < (int)m_bindings.size(); ++i) {
        if (&m_bindings[i] == &binding) {
            binding_index = i;
            break;
        }
    }

    const std::string old_value = binding.pending;
    const float numeric = SliderValueFromMouse(binding, mouse_x);
    const std::string next = format_float(numeric, def.decimals);
    settings_debug("RmlUi settings slider row=%d cvar=%s old=%s new=%s source=%s min=%g max=%g step=%g\n",
                   binding_index,
                   def.cvar ? def.cvar : "<none>",
                   old_value.c_str(),
                   next.c_str(),
                   source ? source : "mouse",
                   def.min_value, def.max_value, def.step);
    if (next != old_value) {
        SetPending(binding, next);
    }
}

void SettingsMenu::Apply()
{
    UI_StartSound(QMS_IN);
    SyncAllFromCvars();
    FlushConfig();
}

void SettingsMenu::ResetDefaults(bool all)
{
    for (Binding& binding : m_bindings) {
        const Def& def = *binding.def;
        if (!all && def.tab != m_active_tab) continue;
        if (!def.cvar || def.kind == Kind::Keybind || def.kind == Kind::Link) continue;
        if (def.kind == Kind::Bitmask) {
            int raw = def.default_value ? std::atoi(def.default_value) : 0;
            bool bit_on = (raw & (1 << def.bit)) != 0;
            SetPending(binding, (def.inverted ? !bit_on : bit_on) ? "1" : "0");
        } else if (def.kind == Kind::Toggle && def.inverted) {
            SetPending(binding, def.default_value && std::string(def.default_value) == "0" ? "1" : "0");
        } else {
            SetPending(binding, def.default_value ? def.default_value : "");
        }
    }
    SyncDirtyState();
}

void SettingsMenu::Activate(Binding& binding, const char *action)
{
    const Def& def = *binding.def;
    if (!binding.available && def.kind != Kind::Keybind && def.kind != Kind::Link) {
        UI_StartSound(QMS_OUT);
        return;
    }
    UI_StartSound((def.kind == Kind::Toggle || def.kind == Kind::Bitmask || def.kind == Kind::Slider || def.kind == Kind::Dropdown) ? QMS_MOVE : QMS_IN);
    if (def.kind == Kind::Toggle || def.kind == Kind::Bitmask) {
        SetPending(binding, binding.pending == "0" ? "1" : "0");
    } else if (def.kind == Kind::Slider) {
        float dir = action && !std::strcmp(action, "-1") ? -1.0f : 1.0f;
        const float old_value = (float)std::atof(binding.pending.c_str());
        float new_value = clampf(old_value + def.step * dir, def.min_value, def.max_value);
        int binding_index = -1;
        for (int i = 0; i < (int)m_bindings.size(); ++i) {
            if (&m_bindings[i] == &binding) {
                binding_index = i;
                break;
            }
        }
        settings_debug("RmlUi settings slider row=%d cvar=%s old=%s new=%s source=%s min=%g max=%g step=%g\n",
                       binding_index,
                       def.cvar ? def.cvar : "<none>",
                       format_float(old_value, def.decimals).c_str(),
                       format_float(new_value, def.decimals).c_str(),
                       (action && !std::strcmp(action, "-1")) ? "left" : "right",
                       def.min_value, def.max_value, def.step);
        SetPending(binding, format_float(new_value, def.decimals));
    } else if (def.kind == Kind::Dropdown) {
        std::vector<Choice> choices = choices_for_def(def);
        if (choices.empty()) {
            return;
        }
        int i = OptionIndex(def, binding.pending);
        int dir = action && !std::strcmp(action, "-1") ? -1 : 1;
        i = (i + dir + (int)choices.size()) % (int)choices.size();
        SetPending(binding, choices[i].value);
    } else if (def.kind == Kind::Text) {
        StartTextEdit(binding);
    } else if (def.kind == Kind::Keybind) {
        StartRebind(binding);
    } else if (def.kind == Kind::Link && def.command) {
        Cbuf_AddText(&cmd_buffer, def.command);
        Cbuf_AddText(&cmd_buffer, "\n");
        m_needs_config_write = true;
        SyncAllFromCvars();
    }
}

void SettingsMenu::FocusBinding(int index, bool scroll_into_view)
{
    if (index < 0 || index >= (int)m_bindings.size()) {
        return;
    }
    if (m_focus_index >= 0 && m_focus_index < (int)m_bindings.size() && m_bindings[m_focus_index].row) {
        m_bindings[m_focus_index].row->SetClass("focused", false);
    }
    m_focus_index = index;
    if (m_bindings[m_focus_index].row) {
        m_bindings[m_focus_index].row->SetClass("focused", true);
        if (scroll_into_view) {
            m_bindings[m_focus_index].row->ScrollIntoView(true);
        }
    }
}

void SettingsMenu::AdjustFocused(int dir)
{
    if (m_bindings.empty()) {
        return;
    }
    int index = m_focus_index < 0 ? 0 : m_focus_index;
    index = (index + dir + (int)m_bindings.size()) % (int)m_bindings.size();
    FocusBinding(index, true);
    UI_StartSound(QMS_MOVE);
}

bool SettingsMenu::ScrollContent(float pixels)
{
    if (!m_content) {
        return false;
    }

    const float sh = m_content->GetScrollHeight();
    const float ch = m_content->GetClientHeight();
    if (!(sh == sh) || !(ch == ch)) {
        return false;
    }
    const float max_scroll = std::max(0.0f, sh - ch);
    const float old_top = m_content->GetScrollTop();
    const float next_top = clampf(old_top + pixels, 0.0f, max_scroll);
    if (!(next_top == next_top)) {
        return false;
    }
    m_content->SetScrollTop(next_top);
    return std::fabs(next_top - old_top) > 0.5f;
}

void SettingsMenu::StartRebind(Binding& binding)
{
    for (int i = 0; i < (int)m_bindings.size(); ++i) {
        if (&m_bindings[i] == &binding) {
            m_rebind_index = i;
            break;
        }
    }
    if (binding.control) {
        binding.control->SetClass("waiting", true);
        SetText(binding.control, "...");
    }
}

void SettingsMenu::FinishRebind(int key)
{
    if (m_rebind_index < 0 || m_rebind_index >= (int)m_bindings.size()) return;
    Binding& binding = m_bindings[m_rebind_index];
    if (key != K_ESCAPE) {
        Key_SetBinding(key, binding.def->cvar);
        m_needs_config_write = true;
    }
    CancelRebind();
    SyncBinding(binding);
}

void SettingsMenu::CancelRebind()
{
    if (m_rebind_index >= 0 && m_rebind_index < (int)m_bindings.size()) {
        Binding& binding = m_bindings[m_rebind_index];
        if (binding.control) binding.control->SetClass("waiting", false);
        SyncBinding(binding);
    }
    m_rebind_index = -1;
}

void SettingsMenu::StartTextEdit(Binding& binding)
{
    for (int i = 0; i < (int)m_bindings.size(); ++i) {
        if (&m_bindings[i] == &binding) {
            m_text_index = i;
            break;
        }
    }
    m_text_original = binding.pending;
    m_text_cursor = (int)binding.pending.size();
    if (binding.control) {
        binding.control->SetClass("waiting", true);
    }
    SyncBinding(binding);
}

void SettingsMenu::FinishTextEdit(bool cancel)
{
    if (m_text_index >= 0 && m_text_index < (int)m_bindings.size()) {
        Binding& binding = m_bindings[m_text_index];
        if (cancel) {
            binding.pending = m_text_original;
        }
        if (binding.control) {
            binding.control->SetClass("waiting", false);
        }
        SetPending(binding, binding.pending);
        SyncDirtyState();
    }
    m_text_index = -1;
    m_text_cursor = -1;
    m_text_original.clear();
}

bool SettingsMenu::HandleKeyEvent(int key, bool down)
{
    if (!m_visible || !down) return false;
    if (m_text_index >= 0) {
        if (m_text_index >= (int)m_bindings.size()) {
            m_text_index = -1;
            m_text_cursor = -1;
            return false;
        }
        Binding& binding = m_bindings[m_text_index];
        if (m_text_cursor < 0 || m_text_cursor > (int)binding.pending.size()) {
            m_text_cursor = (int)binding.pending.size();
        }
        if (key == K_ESCAPE) {
            FinishTextEdit(true);
            return true;
        }
        if (key == K_ENTER) {
            FinishTextEdit(false);
            return true;
        }
        if (key == K_LEFTARROW) {
            if (m_text_cursor > 0) {
                --m_text_cursor;
                SyncBinding(binding);
            }
            return true;
        }
        if (key == K_RIGHTARROW) {
            if (m_text_cursor < (int)binding.pending.size()) {
                ++m_text_cursor;
                SyncBinding(binding);
            }
            return true;
        }
        if (key == K_HOME) {
            m_text_cursor = 0;
            SyncBinding(binding);
            return true;
        }
        if (key == K_END) {
            m_text_cursor = (int)binding.pending.size();
            SyncBinding(binding);
            return true;
        }
        if (key == K_BACKSPACE) {
            if (m_text_cursor > 0 && !binding.pending.empty()) {
                binding.pending.erase((size_t)m_text_cursor - 1, 1);
                --m_text_cursor;
                SyncBinding(binding);
            }
            return true;
        }
        if (key == K_DEL) {
            if (m_text_cursor < (int)binding.pending.size()) {
                binding.pending.erase((size_t)m_text_cursor, 1);
                SyncBinding(binding);
            }
            return true;
        }
        /* Printable ASCII is delivered again via UI_CharEvent / HandleTextInput — avoid double insert. */
        if (key >= 32 && key < 127) {
            return true;
        }
        return false;
    }
    if (m_rebind_index >= 0) {
        if (key == K_BACKSPACE || key == K_DEL) {
            Binding& binding = m_bindings[m_rebind_index];
            for (int k = 0;; ++k) {
                k = Key_EnumBindings(k, binding.def->cvar);
                if (k == -1) break;
                Key_SetBinding(k, nullptr);
                m_needs_config_write = true;
            }
            CancelRebind();
            return true;
        }
        FinishRebind(key);
        return true;
    }
    if (m_showing_root) {
        if (key == K_ESCAPE) {
            RequestBack();
            return true;
        }
        if (key == K_MWHEELUP || key == K_MWHEELDOWN || key == K_PGUP || key == K_PGDN) {
            const float amount = (key == K_PGUP || key == K_PGDN) ? 420.0f : 132.0f;
            ScrollContent((key == K_MWHEELUP || key == K_PGUP) ? -amount : amount);
            return true;
        }
        return false;
    }
    if (m_showing_section_menu) {
        if (key == K_ESCAPE) {
            RequestBack();
            return true;
        }
        if (key == K_MWHEELUP || key == K_MWHEELDOWN || key == K_PGUP || key == K_PGDN) {
            const float amount = (key == K_PGUP || key == K_PGDN) ? 420.0f : 132.0f;
            ScrollContent((key == K_MWHEELUP || key == K_PGUP) ? -amount : amount);
            return true;
        }
        return false;
    }
    if (key == K_ESCAPE) {
        RequestBack();
        return true;
    }
    if (key == K_MWHEELUP || key == K_MWHEELDOWN) {
        ScrollContent(key == K_MWHEELUP ? -132.0f : 132.0f);
        return true;
    }
    if (key == K_PGUP || key == K_PGDN) {
        ScrollContent(key == K_PGUP ? -420.0f : 420.0f);
        return true;
    }
    if (key == K_UPARROW) {
        AdjustFocused(-1);
        return true;
    }
    if (key == K_DOWNARROW || key == K_TAB) {
        AdjustFocused(1);
        return true;
    }
    if (key == K_LEFTARROW || key == K_RIGHTARROW) {
        if (m_focus_index >= 0 && m_focus_index < (int)m_bindings.size()) {
            Activate(m_bindings[m_focus_index], key == K_LEFTARROW ? "-1" : "1");
        }
        return true;
    }
    if (key == K_ENTER || key == K_SPACE) {
        if (m_focus_index >= 0 && m_focus_index < (int)m_bindings.size()) {
            Activate(m_bindings[m_focus_index], nullptr);
        }
        return true;
    }
    return false;
}

bool SettingsMenu::HandleTextInput(int key)
{
    if (!m_visible || m_text_index < 0 || m_text_index >= (int)m_bindings.size()) {
        return false;
    }
    if (key < 32) {
        return false;
    }
    Binding& binding = m_bindings[m_text_index];
    std::string ins;
    if ((unsigned)key < 0x80u) {
        ins.assign(1, (char)key);
    } else {
        ins = utf8_from_codepoint((unsigned)key);
        if (ins.empty()) {
            return false;
        }
    }
    if (binding.pending.size() + ins.size() > 256u) {
        return true;
    }
    binding.pending.insert((size_t)m_text_cursor, ins);
    m_text_cursor += (int)ins.size();
    SyncBinding(binding);
    return true;
}

bool SettingsMenu::HandleMouseButton(int x, int y, bool down)
{
    if (!m_visible || !down) {
        return false;
    }

    if (m_showing_root) {
        for (int i = 0; i < (int)SettingsTab::COUNT; ++i) {
            Rml::Element *button = m_tab_btns[i];
            if (!button) {
                continue;
            }
            const Rml::Vector2f pos = button->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = button->GetBox().GetSize(Rml::BoxArea::Border);
            if (size.x <= 0.0f || size.y <= 0.0f) {
                continue;
            }
            if ((float)x >= pos.x && (float)x <= pos.x + size.x &&
                (float)y >= pos.y && (float)y <= pos.y + size.y) {
                SwitchTab((SettingsTab)i);
                return true;
            }
        }
    } else if (m_showing_section_menu) {
        for (int i = 0; i < (int)m_section_btns.size() && i < (int)m_section_names.size(); ++i) {
            Rml::Element *button = m_section_btns[i];
            if (!button) {
                continue;
            }
            const Rml::Vector2f pos = button->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = button->GetBox().GetSize(Rml::BoxArea::Border);
            if (size.x <= 0.0f || size.y <= 0.0f) {
                continue;
            }
            if ((float)x >= pos.x && (float)x <= pos.x + size.x &&
                (float)y >= pos.y && (float)y <= pos.y + size.y) {
                SwitchSection(m_section_names[i]);
                return true;
            }
        }
    }

    if (m_back) {
        const Rml::Vector2f pos = m_back->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = m_back->GetBox().GetSize(Rml::BoxArea::Border);
        if ((float)x >= pos.x && (float)x <= pos.x + size.x &&
            (float)y >= pos.y && (float)y <= pos.y + size.y) {
            RequestBack();
            return true;
        }
    }

    return false;
}

void SettingsMenu::RequestBack()
{
    UI_StartSound(QMS_OUT);
    if (!m_showing_root) {
        FlushConfig();
        if (!m_showing_section_menu && !m_active_section.empty()) {
            BuildSectionMenu(m_active_tab);
            return;
        }
        ShowRoot();
        return;
    }
    Hide();
    m_close_requested = true;
}

void SettingsMenu::ProcessEvent(Rml::Event& event)
{
    if (!(event == Rml::EventId::Click) &&
        !(event == Rml::EventId::Mouseover) &&
        !(event == Rml::EventId::Mousemove) &&
        !(event == Rml::EventId::Mousedown) &&
        !(event == Rml::EventId::Mouseup) &&
        !(event == Rml::EventId::Mouseout)) {
        return;
    }
    Rml::Element *element = event.GetCurrentElement();
    Rml::Element *target = event.GetTargetElement();
    if (!element && !target) return;

    Rml::Element *bind_element = event_element_with_attribute(event, "data-settings-bind");
    const int index = bind_element ? bind_element->GetAttribute<int>("data-settings-bind", -1) : -1;

    if (event == Rml::EventId::Mousedown && m_showing_root) {
        Rml::Element *tab_element = event_element_with_attribute(event, "data-settings-tab");
        int tab = tab_element ? tab_element->GetAttribute<int>("data-settings-tab", -1) : -1;
        if (tab >= 0 && tab < (int)SettingsTab::COUNT) {
            SwitchTab((SettingsTab)tab);
            event.StopPropagation();
            return;
        }
    }
    if (event == Rml::EventId::Mousedown && m_showing_section_menu) {
        Rml::Element *section_element = event_element_with_attribute(event, "data-settings-section");
        int section = section_element ? section_element->GetAttribute<int>("data-settings-section", -1) : -1;
        if (section >= 0 && section < (int)m_section_names.size()) {
            SwitchSection(m_section_names[section]);
            event.StopPropagation();
            return;
        }
    }

    if (event == Rml::EventId::Mousemove && m_drag_slider_index >= 0 && m_drag_slider_index < (int)m_bindings.size()) {
        SetSliderFromMouse(m_bindings[m_drag_slider_index], event.GetParameter<float>("mouse_x", 0.0f), "drag");
        event.StopPropagation();
        return;
    }

    if (event == Rml::EventId::Mouseup && m_drag_slider_index >= 0 && m_drag_slider_index < (int)m_bindings.size()) {
        Binding& binding = m_bindings[m_drag_slider_index];
        SetSliderFromMouse(binding, event.GetParameter<float>("mouse_x", 0.0f), "release");
        if (binding.slider_wrap) {
            binding.slider_wrap->SetClass("dragging", false);
        }
        if (binding.row) {
            binding.row->SetClass("active", false);
        }
        m_drag_slider_index = -1;
        event.StopPropagation();
        return;
    }

    if (event == Rml::EventId::Mouseover) {
        if (index >= 0 && index < (int)m_bindings.size() && index != m_focus_index) {
            FocusBinding(index);
            UI_StartSound(QMS_MOVE);
        }
        return;
    }

    if (event == Rml::EventId::Mousedown && index >= 0 && index < (int)m_bindings.size()) {
        Binding& binding = m_bindings[index];
        if (binding.def->kind == Kind::Slider && bind_element == binding.slider_wrap) {
            m_drag_slider_index = index;
            FocusBinding(index);
            if (binding.slider_wrap) {
                binding.slider_wrap->SetClass("dragging", true);
            }
            if (binding.row) {
                binding.row->SetClass("active", true);
            }
            SetSliderFromMouse(binding, event.GetParameter<float>("mouse_x", 0.0f), "press");
            event.StopPropagation();
            return;
        }
    }

    if (!(event == Rml::EventId::Click)) {
        return;
    }
    Rml::Element *action_element = event_element_with_attribute(event, "data-settings-action");
    std::string action = action_element ? action_element->GetAttribute<Rml::String>("data-settings-action", "") : "";
    if (!action.empty()) {
        if (action == "apply") Apply();
        else if (action == "reset") ResetDefaults(false);
        else if (action == "reset-all") ResetDefaults(true);
        else if (action == "back") RequestBack();
        else if (action == "confirm-cancel") SetDisplay(m_confirm, false);
        else if (action == "confirm-discard") { SyncAllFromCvars(); Hide(); m_close_requested = true; }
        event.StopPropagation();
        return;
    }
    Rml::Element *tab_element = event_element_with_attribute(event, "data-settings-tab");
    int tab = tab_element ? tab_element->GetAttribute<int>("data-settings-tab", -1) : -1;
    if (tab >= 0 && tab < (int)SettingsTab::COUNT) {
        SwitchTab((SettingsTab)tab);
        event.StopPropagation();
        return;
    }
    Rml::Element *section_element = event_element_with_attribute(event, "data-settings-section");
    int section = section_element ? section_element->GetAttribute<int>("data-settings-section", -1) : -1;
    if (section >= 0 && section < (int)m_section_names.size()) {
        SwitchSection(m_section_names[section]);
        event.StopPropagation();
        return;
    }
    if (index >= 0 && index < (int)m_bindings.size()) {
        std::string step = bind_element ? bind_element->GetAttribute<Rml::String>("data-settings-step", "") : "";
        FocusBinding(index);
        if (!step.empty()) {
            Activate(m_bindings[index], step.c_str());
            event.StopPropagation();
            return;
        }

        // Slider rows are changed by +/- buttons, keyboard arrows, or direct track/drag input.
        if (m_bindings[index].def->kind == Kind::Slider) {
            event.StopPropagation();
            return;
        }

        Activate(m_bindings[index], nullptr);
        event.StopPropagation();
    }
}
