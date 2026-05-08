#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "shared/shared.h"
#ifdef __cplusplus
}
#undef inline
#undef min
#undef max
#undef DotProduct
#undef CrossProduct
#endif

#ifdef __cplusplus
namespace Rml { class RenderInterface; }
Rml::RenderInterface *UI_Rml_CreateRenderInterface(void);
void UI_Rml_DestroyRenderInterface(Rml::RenderInterface *renderer);
extern "C" {
#endif

typedef struct ui_rml_hud_state_s {
    int health;
    int armor;
    int ammo;
    int ammo_max;
    int ammo_reserve;
    int kills;
    int selected_item;
    int layouts;
    float hud_scale;
    float hud_alpha;
    float pickup_alpha;
    const char *weapon_name;
    const char *pickup;
} ui_rml_hud_state_t;

extern cvar_t *ui_rmlui;
extern cvar_t *ui_rmlui_debug;
extern cvar_t *ui_rmlui_show_bounds;
extern cvar_t *cl_crosshair_code;
extern cvar_t *ui_rml_hud_scale;
extern cvar_t *ui_crosshair_mode;

void UI_Rml_Init(void);
void UI_Rml_Shutdown(void);
void UI_Rml_NewFrame(int width, int height, double time_seconds);
void UI_Rml_Render(void);
void UI_Rml_Reload(void);
bool UI_Rml_OpenDocument(const char *name);
void UI_Rml_CloseDocument(const char *name);
bool UI_Rml_OpenMenuDocument(const char *name, bool push_history);
void UI_Rml_Back(void);
void UI_Rml_SetHudState(const ui_rml_hud_state_t *state);
void UI_Rml_UpdateCrosshair(float dt, float speed, bool on_ground, bool crouched, bool just_fired, int layouts);
bool UI_Rml_UseLegacyCrosshair(void);
void UI_Rml_SetMenuState(int menu_type, bool paused);
void UI_Rml_ToggleCrosshairMenu(void);
void UI_Rml_ToggleSettingsMenu(void);
bool UI_Rml_HandleKeyEvent(int key, bool down);
bool UI_Rml_HandleMouseMove(int x, int y);
bool UI_Rml_HandleMouseButton(int key, bool down);
bool UI_Rml_HandleTextInput(int key);
bool UI_Rml_IsEnabled(void);
bool UI_Rml_IsMenuOpen(void);
bool UI_Rml_IsTransparent(void);
void UI_Rml_ForceMenuOff(void);

#ifdef __cplusplus
}
#endif
