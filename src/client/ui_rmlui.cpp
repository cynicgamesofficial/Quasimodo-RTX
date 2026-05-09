#include "ui_rmlui.h"
#include "hud/crosshair.h"
#include "hud/crosshair_menu.h"
#include "hud/preset_competitive.h"
#include "intro/intro_sequence.h"
#include "ui/settings_menu.h"

extern "C" {
#include "common/cmd.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/zone.h"
#include "refresh/refresh.h"
#include "client/keys.h"

void UI_StartSound(int sound);
bool OGG_PlayMenuTrack(const char *track);
void OGG_StopMenuTrack(void);
extern void (*R_ClearColor)(void);
extern void (*R_SetAlphaScale)(float alpha);
extern void (*R_SetScale)(float scale);
extern void (*R_DrawFill32)(int x, int y, int w, int h, uint32_t color);
}

#define QMS_IN          2
#define QMS_MOVE        3
#define QMS_OUT         4

extern "C" bool UI_Rml_OpenMenuDocument(const char *name, bool push_history);
extern "C" void UI_Rml_Back(void);
extern "C" void UI_Rml_SetMenuState(int menu_type, bool paused);

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

cvar_t *ui_rmlui;
cvar_t *ui_splash;
cvar_t *ui_rmlui_debug;
cvar_t *ui_rmlui_show_bounds;
static cvar_t *ui_rmlui_skip_intro;
static cvar_t *ui_rmlui_crosshair_debug;
cvar_t *cl_crosshair_code;
cvar_t *ui_rml_hud_scale;
cvar_t *ui_crosshair_mode;
cvar_t *ui_title_music;
cvar_t *ui_crosshair_style;
cvar_t *ui_crosshair_dynamic;
cvar_t *ui_crosshair_dot;
cvar_t *ui_crosshair_outline;
cvar_t *ui_crosshair_tstyle;
cvar_t *ui_crosshair_size;
cvar_t *ui_crosshair_thickness;
cvar_t *ui_crosshair_gap;
cvar_t *ui_crosshair_opacity;
cvar_t *ui_crosshair_red;
cvar_t *ui_crosshair_green;
cvar_t *ui_crosshair_blue;

namespace {

struct RmlFile {
    char *data = nullptr;
    size_t length = 0;
    size_t pos = 0;
};

class Q2RmlSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return time_seconds; }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        const char *prefix = "RmlUi";
        switch (type) {
        case Rml::Log::LT_ERROR: prefix = "RmlUi error"; break;
        case Rml::Log::LT_WARNING: prefix = "RmlUi warning"; break;
        default: break;
        }
        Com_LPrintf(PRINT_ALL, "%s: %s\n", prefix, message.c_str());
        return true;
    }

    void set_time(double t) { time_seconds = t; }

private:
    double time_seconds = 0.0;
};

class Q2RmlFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override
    {
        void *buffer = nullptr;
        int length = FS_LoadFileEx(path.c_str(), &buffer, 0, TAG_FILESYSTEM);
        if (length < 0 || !buffer) {
            return 0;
        }

        auto *file = new RmlFile;
        file->data = static_cast<char *>(buffer);
        file->length = static_cast<size_t>(length);
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<RmlFile *>(handle);
        if (!file) {
            return;
        }
        Z_Free(file->data);
        delete file;
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<RmlFile *>(handle);
        if (!file || file->pos >= file->length) {
            return 0;
        }
        const size_t bytes = std::min(size, file->length - file->pos);
        memcpy(buffer, file->data + file->pos, bytes);
        file->pos += bytes;
        return bytes;
    }

    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        auto *file = reinterpret_cast<RmlFile *>(handle);
        if (!file) {
            return false;
        }
        long base = 0;
        if (origin == SEEK_CUR) {
            base = static_cast<long>(file->pos);
        } else if (origin == SEEK_END) {
            base = static_cast<long>(file->length);
        }
        const long next = base + offset;
        if (next < 0 || static_cast<size_t>(next) > file->length) {
            return false;
        }
        file->pos = static_cast<size_t>(next);
        return true;
    }

    size_t Tell(Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<RmlFile *>(handle);
        return file ? file->pos : 0;
    }

    size_t Length(Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<RmlFile *>(handle);
        return file ? file->length : 0;
    }
};

bool parse_settings_tab(const char *name, SettingsTab& tab);
void show_settings_menu(SettingsTab tab);
void show_settings_section(SettingsTab tab, const char *section);
void show_settings_root();
void debug_log(const char *fmt, ...);

class CommandListener final : public Rml::EventListener {
public:
    explicit CommandListener(std::string command) : command(std::move(command)) {}

    void ProcessEvent(Rml::Event& event) override
    {
        if (!(event == Rml::EventId::Click) || command.empty()) {
            return;
        }

        if (command.rfind("rmlmenu ", 0) == 0) {
            UI_StartSound(QMS_IN);
            UI_Rml_OpenMenuDocument(command.c_str() + 8, true);
            return;
        }
        if (command.rfind("rmlsettings", 0) == 0) {
            const char *tab_name = command.size() > 12 ? command.c_str() + 12 : "";
            while (*tab_name == ' ') {
                ++tab_name;
            }

            if (!tab_name[0]) {
                UI_StartSound(QMS_IN);
                show_settings_root();
                return;
            }

            SettingsTab tab = SettingsTab::Game;
            if (!parse_settings_tab(tab_name, tab)) {
                Com_LPrintf(PRINT_WARNING, "RmlUi: unknown settings tab '%s'\n", tab_name);
            }
            UI_StartSound(QMS_IN);
            show_settings_menu(tab);
            return;
        }
        if (command == "rmlback") {
            UI_StartSound(QMS_OUT);
            UI_Rml_Back();
            return;
        }

        UI_StartSound(QMS_IN);
        Cbuf_AddText(&cmd_buffer, command.c_str());
        if (command.back() != '\n') {
            Cbuf_AddText(&cmd_buffer, "\n");
        }
    }

private:
    std::string command;
};

struct RmlUiState {
    bool initialized = false;
    bool menu_open = false;
    bool menu_transparent = false;
    int width = 0;
    int height = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    int menu_type = 0;
    double last_time_seconds = 0.0;
    bool intro_done = false;
    bool pending_menu = false;
    int pending_menu_type = 0;
    bool pending_menu_paused = false;
    Q2RmlSystem system;
    Q2RmlFileInterface files;
    Rml::RenderInterface *renderer = nullptr;
    Rml::Context *context = nullptr;
    std::string active_menu_document;
    std::vector<std::string> menu_stack;
    std::unordered_map<std::string, Rml::ElementDocument *> documents;
    std::vector<std::unique_ptr<CommandListener>> listeners;
    IntroSequence intro;
    CompetitiveHUD competitive_hud;
    CrosshairHUD crosshair;
    std::string last_crosshair_code;
    CrosshairMenu crosshair_menu;
    SettingsMenu settings_menu;
    bool last_crosshair_hidden = true;
    bool last_crosshair_mode = false;
    int last_crosshair_layouts = -1;
    float last_hud_scale = -1.0f;
    double last_crosshair_motion_log_time = -1.0;
    bool log_next_crosshair_diag = true;
    bool log_next_hud_diag = true;
    bool logged_render_path = false;
    int last_logged_health = -1;
    int last_logged_armor = -1;
    char last_weapon_name[64] = {};
};

RmlUiState rmlui;

Rml::Input::KeyIdentifier map_key(int key)
{
    if (key >= 'a' && key <= 'z') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + key - 'a');
    if (key >= 'A' && key <= 'Z') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + key - 'A');
    if (key >= '0' && key <= '9') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + key - '0');

    switch (key) {
    case K_ESCAPE: return Rml::Input::KI_ESCAPE;
    case K_ENTER: return Rml::Input::KI_RETURN;
    case K_TAB: return Rml::Input::KI_TAB;
    case K_BACKSPACE: return Rml::Input::KI_BACK;
    case K_UPARROW: return Rml::Input::KI_UP;
    case K_DOWNARROW: return Rml::Input::KI_DOWN;
    case K_LEFTARROW: return Rml::Input::KI_LEFT;
    case K_RIGHTARROW: return Rml::Input::KI_RIGHT;
    case K_HOME: return Rml::Input::KI_HOME;
    case K_END: return Rml::Input::KI_END;
    case K_PGUP: return Rml::Input::KI_PRIOR;
    case K_PGDN: return Rml::Input::KI_NEXT;
    case K_INS: return Rml::Input::KI_INSERT;
    case K_DEL: return Rml::Input::KI_DELETE;
    case K_SPACE: return Rml::Input::KI_SPACE;
    default: return Rml::Input::KI_UNKNOWN;
    }
}

int key_modifiers()
{
    int modifiers = 0;
    if (Key_IsDown(K_SHIFT)) modifiers |= Rml::Input::KM_SHIFT;
    if (Key_IsDown(K_CTRL)) modifiers |= Rml::Input::KM_CTRL;
    if (Key_IsDown(K_ALT)) modifiers |= Rml::Input::KM_ALT;
    return modifiers;
}

void bind_commands(Rml::ElementDocument *doc)
{
    if (!doc) {
        return;
    }

    Rml::ElementList elements;
    doc->QuerySelectorAll(elements, "[data-command]");
    for (Rml::Element *element : elements) {
        std::string command = element->GetAttribute<Rml::String>("data-command", "");
        auto listener = std::make_unique<CommandListener>(command);
        element->AddEventListener(Rml::EventId::Click, listener.get());
        rmlui.listeners.push_back(std::move(listener));
    }
}

bool is_menu_document_name(const char *name)
{
    if (!name) {
        return false;
    }
    return !strcmp(name, "main_menu") ||
           !strcmp(name, "pause_menu") ||
           !strcmp(name, "options") ||
           !strcmp(name, "developer") ||
           !strcmp(name, "video");
}

void update_mouse_position(int x, int y)
{
    const int max_x = std::max(0, (rmlui.width > 0 ? rmlui.width : r_config.width) - 1);
    const int max_y = std::max(0, (rmlui.height > 0 ? rmlui.height : r_config.height) - 1);
    rmlui.mouse_x = std::max(0, std::min(x, max_x));
    rmlui.mouse_y = std::max(0, std::min(y, max_y));
}

void refresh_menu_mouse_position()
{
    update_mouse_position(rmlui.mouse_x, rmlui.mouse_y);
}

bool parse_settings_tab(const char *name, SettingsTab& tab)
{
    if (!name || !name[0]) {
        tab = SettingsTab::Game;
        return true;
    }

    if (!strcmp(name, "graphics") || !strcmp(name, "video")) {
        tab = SettingsTab::Video;
        return true;
    }
    if (!strcmp(name, "rtx")) {
        tab = SettingsTab::RTX;
        return true;
    }
    if (!strcmp(name, "environment") || !strcmp(name, "env")) {
        tab = SettingsTab::Environment;
        return true;
    }
    if (!strcmp(name, "advanced") || !strcmp(name, "developer") || !strcmp(name, "debug")) {
        tab = SettingsTab::Developer;
        return true;
    }
    if (!strcmp(name, "effects")) {
        tab = SettingsTab::Effects;
        return true;
    }
    if (!strcmp(name, "audio") || !strcmp(name, "sound")) {
        tab = SettingsTab::Audio;
        return true;
    }
    if (!strcmp(name, "controls") || !strcmp(name, "input")) {
        tab = SettingsTab::Controls;
        return true;
    }
    if (!strcmp(name, "game")) {
        tab = SettingsTab::Game;
        return true;
    }
    if (!strcmp(name, "crosshair")) {
        tab = SettingsTab::Game;
        return true;
    }
    if (!strcmp(name, "network") || !strcmp(name, "multiplayer")) {
        tab = SettingsTab::Game;
        return true;
    }
    tab = SettingsTab::Game;
    return false;
}

void show_settings_menu(SettingsTab tab)
{
    const bool transparent = rmlui.menu_transparent;

    if (rmlui.crosshair_menu.IsVisible()) {
        rmlui.crosshair_menu.Hide();
    }

    if (!rmlui.active_menu_document.empty()) {
        rmlui.menu_stack.push_back(rmlui.active_menu_document);
        UI_Rml_CloseDocument(rmlui.active_menu_document.c_str());
        rmlui.active_menu_document.clear();
    }

    rmlui.settings_menu.SetTransparentBackground(transparent);
    rmlui.settings_menu.Show(tab);
    rmlui.menu_open = true;
    refresh_menu_mouse_position();
    Key_SetDest(static_cast<keydest_t>(Key_GetDest() | KEY_MENU));
    debug_log("RmlUi: show settings tab=%d active_doc=%s\n",
              static_cast<int>(tab),
              rmlui.active_menu_document.empty() ? "<none>" : rmlui.active_menu_document.c_str());
}

void show_settings_section(SettingsTab tab, const char *section)
{
    const bool transparent = rmlui.menu_transparent;

    if (rmlui.crosshair_menu.IsVisible()) {
        rmlui.crosshair_menu.Hide();
    }

    if (!rmlui.active_menu_document.empty()) {
        rmlui.menu_stack.push_back(rmlui.active_menu_document);
        UI_Rml_CloseDocument(rmlui.active_menu_document.c_str());
        rmlui.active_menu_document.clear();
    }

    rmlui.settings_menu.SetTransparentBackground(transparent);
    rmlui.settings_menu.ShowSection(tab, section);
    rmlui.menu_open = true;
    refresh_menu_mouse_position();
    Key_SetDest(static_cast<keydest_t>(Key_GetDest() | KEY_MENU));
    debug_log("RmlUi: show settings tab=%d section=%s active_doc=%s\n",
              static_cast<int>(tab),
              section ? section : "",
              rmlui.active_menu_document.empty() ? "<none>" : rmlui.active_menu_document.c_str());
}

void show_settings_root()
{
    const bool transparent = rmlui.menu_transparent;

    if (rmlui.crosshair_menu.IsVisible()) {
        rmlui.crosshair_menu.Hide();
    }

    if (!rmlui.active_menu_document.empty()) {
        rmlui.menu_stack.push_back(rmlui.active_menu_document);
        UI_Rml_CloseDocument(rmlui.active_menu_document.c_str());
        rmlui.active_menu_document.clear();
    }

    rmlui.settings_menu.SetTransparentBackground(transparent);
    rmlui.settings_menu.ShowRoot();
    rmlui.menu_open = true;
    refresh_menu_mouse_position();
    Key_SetDest(static_cast<keydest_t>(Key_GetDest() | KEY_MENU));
    debug_log("RmlUi: show settings root active_doc=%s\n",
              rmlui.active_menu_document.empty() ? "<none>" : rmlui.active_menu_document.c_str());
}

void set_text(Rml::ElementDocument *doc, const char *id, const char *value)
{
    if (doc) {
        if (Rml::Element *element = doc->GetElementById(id)) {
            element->SetInnerRML(value ? value : "");
        }
    }
}

// Collect all focusable button elements from a document in DOM order.
static void collect_buttons(Rml::Element *parent, std::vector<Rml::Element *>& out)
{
    if (!parent) {
        return;
    }
    for (int i = 0; i < static_cast<int>(parent->GetNumChildren()); ++i) {
        Rml::Element *child = parent->GetChild(i);
        if (!child) {
            continue;
        }
        const Rml::String tag = child->GetTagName();
        if (tag == "button") {
            const Rml::String display = child->GetProperty<Rml::String>("display");
            if (display != "none") {
                out.push_back(child);
            }
        }
        collect_buttons(child, out);
    }
}

// Move keyboard focus by delta (+1=next, -1=prev) among visible buttons in
// the active menu document.
static bool navigate_menu_focus(int delta)
{
    Rml::ElementDocument *doc = nullptr;
    if (!rmlui.active_menu_document.empty()) {
        auto it = rmlui.documents.find(rmlui.active_menu_document);
        if (it != rmlui.documents.end()) {
            doc = it->second;
        }
    }
    if (!doc) {
        return false;
    }

    std::vector<Rml::Element *> buttons;
    collect_buttons(doc, buttons);
    if (buttons.empty()) {
        return false;
    }

    // Find currently focused button.
    int current = -1;
    Rml::Element *focused = rmlui.context->GetFocusElement();
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        if (buttons[i] == focused) {
            current = i;
            break;
        }
    }

    int next;
    if (current < 0) {
        next = (delta > 0) ? 0 : static_cast<int>(buttons.size()) - 1;
    } else {
        next = (current + delta + static_cast<int>(buttons.size())) % static_cast<int>(buttons.size());
    }

    Rml::Element *target = buttons[next];
    target->Focus();
    UI_StartSound(QMS_MOVE);

    return true;
}

std::string stat_string(int value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

void debug_log(const char *fmt, ...)
{
    if (!ui_rmlui_debug || !ui_rmlui_debug->integer) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Com_LPrintf(PRINT_DEVELOPER, "%s", buffer);
}

bool debug_enabled()
{
    return ui_rmlui_debug && ui_rmlui_debug->integer;
}

bool crosshair_motion_debug_enabled()
{
    return debug_enabled() && ui_rmlui_crosshair_debug && ui_rmlui_crosshair_debug->integer;
}

void process_pending_shell_closes()
{
    if (rmlui.crosshair_menu.TakeCloseRequest()) {
        if (rmlui.active_menu_document.empty() && !rmlui.menu_stack.empty()) {
            UI_Rml_Back();
        } else if (rmlui.active_menu_document.empty()) {
            rmlui.menu_open = false;
            rmlui.menu_transparent = false;
            Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
        }
        refresh_menu_mouse_position();
    }
    if (rmlui.settings_menu.TakeCloseRequest()) {
        if (rmlui.active_menu_document.empty() && !rmlui.crosshair_menu.IsVisible() && !rmlui.menu_stack.empty()) {
            UI_Rml_Back();
        } else if (rmlui.active_menu_document.empty() && !rmlui.crosshair_menu.IsVisible()) {
            rmlui.menu_open = false;
            rmlui.menu_transparent = false;
            Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
        }
        refresh_menu_mouse_position();
    }
}

bool load_font(const char *path)
{
    const bool loaded = Rml::LoadFontFace(path);
    if (loaded) {
        debug_log("RmlUi: loaded font %s\n", path);
    } else {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to load font %s\n", path);
    }
    return loaded;
}

bool use_new_crosshair()
{
    return ui_crosshair_mode && ui_crosshair_mode->integer != 0;
}

void sync_crosshair_config()
{
    CrosshairConfig config;
    config.style = static_cast<CrosshairStyle>(std::max(0, std::min(ui_crosshair_style ? ui_crosshair_style->integer : 0, 3)));
    config.dynamic = ui_crosshair_dynamic && ui_crosshair_dynamic->integer != 0;
    config.dot = !ui_crosshair_dot || ui_crosshair_dot->integer != 0;
    config.outline_thickness = std::max(0, std::min(ui_crosshair_outline ? ui_crosshair_outline->integer : 0, 3));
    config.outline = config.outline_thickness > 0;
    config.t_style = ui_crosshair_tstyle && ui_crosshair_tstyle->integer != 0;
    config.size = std::max(1, std::min(ui_crosshair_size ? ui_crosshair_size->integer : 5, 20));
    config.thickness = std::max(1, std::min(ui_crosshair_thickness ? ui_crosshair_thickness->integer : 1, 5));
    config.gap = std::max(0, std::min(ui_crosshair_gap ? ui_crosshair_gap->integer : 3, 15));
    config.opacity = std::max(0.0f, std::min(ui_crosshair_opacity ? ui_crosshair_opacity->value : 0.85f, 1.0f));
    config.r = std::max(0.0f, std::min(ui_crosshair_red ? ui_crosshair_red->value : 1.0f, 1.0f));
    config.g = std::max(0.0f, std::min(ui_crosshair_green ? ui_crosshair_green->value : 1.0f, 1.0f));
    config.b = std::max(0.0f, std::min(ui_crosshair_blue ? ui_crosshair_blue->value : 1.0f, 1.0f));

    const Rml::String code = CrosshairConfigToString(config);
    if (rmlui.last_crosshair_code == code.c_str()) {
        return;
    }

    rmlui.crosshair.Config() = config;
    rmlui.crosshair.ApplyConfig();
    rmlui.last_crosshair_code = code.c_str();
    debug_log("RmlUi crosshair: config code=%s style=%d size=%d thickness=%d gap=%d dot=%d outline_px=%d opacity=%.3f rgb=(%.3f,%.3f,%.3f)\n",
              code.c_str(),
              static_cast<int>(config.style),
              config.size,
              config.thickness,
              config.gap,
              config.dot ? 1 : 0,
              config.outline ? config.outline_thickness : 0,
              config.opacity,
              config.r,
              config.g,
              config.b);
    rmlui.log_next_crosshair_diag = true;
    if (cl_crosshair_code) {
        Cvar_Set("cl_crosshair_code", code.c_str());
    }
}

bool title_music_playing = false;

bool title_music_enabled()
{
    if (ui_title_music && !ui_title_music->integer) {
        return false;
    }
    if (cvar_t *enabled = Cvar_WeakGet("ogg_enable")) {
        if (!enabled->integer) {
            return false;
        }
    }
    return true;
}

void stop_title_music()
{
    if (!title_music_playing) {
        return;
    }

    OGG_StopMenuTrack();
    title_music_playing = false;
}

void start_title_music()
{
    if (!title_music_enabled()) {
        stop_title_music();
        return;
    }
    if (title_music_playing) {
        return;
    }

    if (OGG_PlayMenuTrack("titlemenu")) {
        title_music_playing = true;
    }
}

void refresh_title_music()
{
    const bool should_play = rmlui.menu_open && !rmlui.menu_transparent && !rmlui.intro.IsPlaying();
    if (should_play) {
        start_title_music();
    } else {
        stop_title_music();
    }
}

void rml_shutdown_hud_modules(void)
{
    rmlui.intro.Shutdown();
    rmlui.settings_menu.Shutdown();
    rmlui.crosshair_menu.Shutdown();
    rmlui.crosshair.Shutdown();
    rmlui.competitive_hud.Shutdown();
}

void rml_clear_listener_registry_and_documents(bool unload_context_documents)
{
    rmlui.listeners.clear();
    if (unload_context_documents && rmlui.context) {
        rmlui.context->UnloadAllDocuments();
    }
    rmlui.documents.clear();
}

} // namespace

extern "C" {

bool UI_Rml_IsEnabled(void)
{
    return ui_rmlui && ui_rmlui->integer && rmlui.initialized && rmlui.context;
}

void UI_Rml_Init(void)
{
    ui_rmlui = Cvar_Get("ui_rmlui", "1", CVAR_ARCHIVE);
    ui_splash = Cvar_Get("ui_splash", "0", CVAR_ARCHIVE);
    ui_rmlui_debug = Cvar_Get("ui_rmlui_debug", "0", 0);
    ui_rmlui_show_bounds = Cvar_Get("ui_rmlui_show_bounds", "0", 0);
    ui_rmlui_skip_intro = Cvar_Get("ui_rmlui_skip_intro", "0", 0);
    ui_rmlui_crosshair_debug = Cvar_Get("ui_rmlui_crosshair_debug", "0", 0);
    ui_rml_hud_scale = Cvar_Get("ui_rml_hud_scale", "3.0", CVAR_ARCHIVE);
    ui_crosshair_mode = Cvar_Get("ui_crosshair_mode", "1", CVAR_ARCHIVE);
    ui_title_music = Cvar_Get("ui_title_music", "1", CVAR_ARCHIVE);
    ui_crosshair_style = Cvar_Get("ui_crosshair_style", "0", CVAR_ARCHIVE);
    ui_crosshair_dynamic = Cvar_Get("ui_crosshair_dynamic", "0", CVAR_ARCHIVE);
    ui_crosshair_dot = Cvar_Get("ui_crosshair_dot", "1", CVAR_ARCHIVE);
    ui_crosshair_outline = Cvar_Get("ui_crosshair_outline", "0", CVAR_ARCHIVE);
    ui_crosshair_tstyle = Cvar_Get("ui_crosshair_tstyle", "0", CVAR_ARCHIVE);
    ui_crosshair_size = Cvar_Get("ui_crosshair_size", "5", CVAR_ARCHIVE);
    ui_crosshair_thickness = Cvar_Get("ui_crosshair_thickness", "1", CVAR_ARCHIVE);
    ui_crosshair_gap = Cvar_Get("ui_crosshair_gap", "3", CVAR_ARCHIVE);
    ui_crosshair_opacity = Cvar_Get("ui_crosshair_opacity", "0.85", CVAR_ARCHIVE);
    ui_crosshair_red = Cvar_Get("ui_crosshair_red", "1", CVAR_ARCHIVE);
    ui_crosshair_green = Cvar_Get("ui_crosshair_green", "1", CVAR_ARCHIVE);
    ui_crosshair_blue = Cvar_Get("ui_crosshair_blue", "1", CVAR_ARCHIVE);
    CrosshairConfig default_crosshair;
    const Rml::String default_crosshair_code = CrosshairConfigToString(default_crosshair);
    cl_crosshair_code = Cvar_Get("cl_crosshair_code", default_crosshair_code.c_str(), CVAR_ARCHIVE);

    if (rmlui.initialized) {
        return;
    }

    rmlui.renderer = UI_Rml_CreateRenderInterface();
    if (!rmlui.renderer) {
        Com_LPrintf(PRINT_ERROR, "RmlUi: renderer creation failed\n");
        return;
    }

    Rml::SetSystemInterface(&rmlui.system);
    Rml::SetFileInterface(&rmlui.files);
    Rml::SetRenderInterface(rmlui.renderer);

    if (!Rml::Initialise()) {
        Com_LPrintf(PRINT_ERROR, "RmlUi: initialization failed\n");
        UI_Rml_DestroyRenderInterface(rmlui.renderer);
        rmlui.renderer = nullptr;
        return;
    }

    rmlui.context = Rml::CreateContext("q2rtx", Rml::Vector2i(std::max(1, r_config.width), std::max(1, r_config.height)));
    if (!rmlui.context) {
        Com_LPrintf(PRINT_ERROR, "RmlUi: context creation failed\n");
        Rml::Shutdown();
        UI_Rml_DestroyRenderInterface(rmlui.renderer);
        rmlui.renderer = nullptr;
        return;
    }

    bool any_font = false;
    any_font |= load_font("ui/fonts/LatoLatin-Regular.ttf");
    any_font |= load_font("ui/fonts/LatoLatin-Bold.ttf");
    any_font |= load_font("ui/fonts/RobotoMono-Regular.ttf");
    any_font |= load_font("ui/fonts/Rajdhani-Regular.ttf");
    any_font |= load_font("ui/fonts/Rajdhani-Bold.ttf");
    if (!any_font) {
        Com_LPrintf(PRINT_ERROR, "RmlUi: no fonts loaded; menu/HUD text will not render\n");
    }

    rmlui.initialized = true;
    rmlui.intro_done = false;
    if (!ui_splash || !ui_splash->integer) {
        rmlui.intro_done = true;
        debug_log("RmlUi: startup splash disabled (ui_splash 0)\n");
    } else if (ui_rmlui_skip_intro && ui_rmlui_skip_intro->integer) {
        rmlui.intro_done = true;
        debug_log("RmlUi: intro skipped by ui_rmlui_skip_intro\n");
    } else if (!rmlui.intro.Init(rmlui.context, []() {
            rmlui.intro_done = true;
            if (rmlui.pending_menu) {
                const int menu_type = rmlui.pending_menu_type;
                const bool paused = rmlui.pending_menu_paused;
                rmlui.pending_menu = false;
                UI_Rml_SetMenuState(menu_type, paused);
            }
        })) {
        rmlui.intro_done = true;
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to initialize intro sequence\n");
    }
    if (!rmlui.competitive_hud.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to initialize Competitive HUD preset\n");
    }
    if (!rmlui.crosshair.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to initialize crosshair HUD\n");
    }
    rmlui.last_crosshair_code.clear();
    rmlui.last_crosshair_hidden = true;
    rmlui.last_crosshair_mode = use_new_crosshair();
    rmlui.last_crosshair_layouts = -1;
    rmlui.last_hud_scale = -1.0f;
    rmlui.last_crosshair_motion_log_time = -1.0;
    rmlui.last_logged_health = -1;
    rmlui.last_logged_armor = -1;
    rmlui.log_next_crosshair_diag = true;
    rmlui.log_next_hud_diag = true;
    rmlui.logged_render_path = false;
    if (!rmlui.settings_menu.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to initialize settings menu\n");
    }
}

void UI_Rml_Shutdown(void)
{
    if (!rmlui.initialized) {
        return;
    }
    stop_title_music();
    rml_shutdown_hud_modules();
    rml_clear_listener_registry_and_documents(false);
    if (rmlui.context) {
        Rml::RemoveContext("q2rtx");
        rmlui.context = nullptr;
    }
    Rml::Shutdown();
    UI_Rml_DestroyRenderInterface(rmlui.renderer);
    rmlui.renderer = nullptr;
    rmlui.initialized = false;
    rmlui.menu_open = false;
    rmlui.menu_transparent = false;
    rmlui.active_menu_document.clear();
    rmlui.menu_stack.clear();
    rmlui.last_crosshair_code.clear();
    rmlui.width = 0;
    rmlui.height = 0;
    rmlui.menu_type = 0;
    rmlui.last_time_seconds = 0.0;
    rmlui.intro_done = false;
    rmlui.pending_menu = false;
    rmlui.last_crosshair_hidden = true;
    rmlui.last_crosshair_mode = false;
    rmlui.last_crosshair_layouts = -1;
    rmlui.last_hud_scale = -1.0f;
    rmlui.last_crosshair_motion_log_time = -1.0;
    rmlui.last_logged_health = -1;
    rmlui.last_logged_armor = -1;
    rmlui.log_next_crosshair_diag = true;
    rmlui.log_next_hud_diag = true;
    rmlui.logged_render_path = false;
}

void UI_Rml_NewFrame(int width, int height, double time_seconds)
{
    if (!UI_Rml_IsEnabled()) {
        return;
    }

    rmlui.system.set_time(time_seconds);
    const float dt = rmlui.last_time_seconds > 0.0 ? static_cast<float>(time_seconds - rmlui.last_time_seconds) : 0.0f;
    rmlui.last_time_seconds = time_seconds;
    if (width != rmlui.width || height != rmlui.height) {
        rmlui.width = width;
        rmlui.height = height;
        rmlui.context->SetDimensions(Rml::Vector2i(std::max(1, width), std::max(1, height)));
        rmlui.log_next_hud_diag = true;
        rmlui.log_next_crosshair_diag = true;
        debug_log("RmlUi: viewport changed %dx%d\n", rmlui.width, rmlui.height);
    }

    if (rmlui.intro.IsPlaying()) {
        rmlui.intro.Update(dt);
    }
    debug_log("RmlUi: update active_doc=%s menu_open=%d transparent=%d settings=%d crosshair_menu=%d intro=%d size=%dx%d\n",
              rmlui.active_menu_document.empty() ? "<none>" : rmlui.active_menu_document.c_str(),
              rmlui.menu_open ? 1 : 0,
              rmlui.menu_transparent ? 1 : 0,
              rmlui.settings_menu.IsVisible() ? 1 : 0,
              rmlui.crosshair_menu.IsVisible() ? 1 : 0,
              rmlui.intro.IsPlaying() ? 1 : 0,
              rmlui.width,
              rmlui.height);
    /*
     * Gameplay HUD state is pushed after UI_Rml_NewFrame() during SCR_DrawActive().
     * Defer Context::Update() until UI_Rml_Render() so RmlUi lays out the exact
     * DOM that will be rendered this frame.
     */
}

void UI_Rml_Render(void)
{
    if (!UI_Rml_IsEnabled()) {
        return;
    }
    process_pending_shell_closes();
    rmlui.intro.FlushDoneCallback();

    R_ClearColor();
    R_SetAlphaScale(1.0f);
    R_SetScale(1.0f);
    if (rmlui.menu_open || rmlui.intro.IsPlaying()) {
        const uint32_t color = rmlui.menu_transparent ? 0xcc0e1208u : 0xff0e1208u;
        R_DrawFill32(0, 0, std::max(1, rmlui.width), std::max(1, rmlui.height), color);
    }
    rmlui.crosshair.SetDebugBounds(ui_rmlui_show_bounds && ui_rmlui_show_bounds->integer);
    /* Crosshair must paint above the competitive HUD document (same context root order). */
    rmlui.crosshair.BringToFront();

    rmlui.context->Update();
    refresh_title_music();

    debug_log("RmlUi: render active_doc=%s menu_open=%d transparent=%d settings=%d crosshair_menu=%d intro=%d viewport=%dx%d path=stretch_pic_overlay\n",
              rmlui.active_menu_document.empty() ? "<none>" : rmlui.active_menu_document.c_str(),
              rmlui.menu_open ? 1 : 0,
              rmlui.menu_transparent ? 1 : 0,
              rmlui.settings_menu.IsVisible() ? 1 : 0,
              rmlui.crosshair_menu.IsVisible() ? 1 : 0,
              rmlui.intro.IsPlaying() ? 1 : 0,
              rmlui.width,
              rmlui.height);
    if (debug_enabled() && !rmlui.logged_render_path) {
        Com_LPrintf(PRINT_DEVELOPER,
            "RmlUi: final render path executes after scene via stretch-pic overlay queue; blend=src_alpha, depth=off, scissor=CPU-clipped/full-target, viewport=%dx%d\n",
            rmlui.width,
            rmlui.height);
        rmlui.logged_render_path = true;
    }
    if (rmlui.log_next_hud_diag) {
        rmlui.competitive_hud.DebugDescribe(debug_enabled(), "post-update pre-render");
        rmlui.log_next_hud_diag = false;
    }
    if (rmlui.log_next_crosshair_diag) {
        rmlui.crosshair.DebugDescribe(debug_enabled(), "post-update pre-render");
        rmlui.log_next_crosshair_diag = false;
    }
    rmlui.context->Render();
}

void UI_Rml_Reload(void)
{
    if (!rmlui.initialized || !rmlui.context) {
        return;
    }

    bool menu_was_open = rmlui.menu_open;
    int menu_type = rmlui.menu_type;
    bool crosshair_menu_was_open = rmlui.crosshair_menu.IsVisible();
    bool settings_menu_was_open = rmlui.settings_menu.IsVisible();
    const bool intro_was_playing = rmlui.intro.IsPlaying();
    rml_shutdown_hud_modules();
    rml_clear_listener_registry_and_documents(true);
    rmlui.menu_open = false;
    rmlui.active_menu_document.clear();
    rmlui.menu_stack.clear();
    if (intro_was_playing && ui_splash && ui_splash->integer && !(ui_rmlui_skip_intro && ui_rmlui_skip_intro->integer)) {
        rmlui.intro_done = false;
        if (!rmlui.intro.Init(rmlui.context, []() {
                rmlui.intro_done = true;
                if (rmlui.pending_menu) {
                    const int pending_type = rmlui.pending_menu_type;
                    const bool pending_paused = rmlui.pending_menu_paused;
                    rmlui.pending_menu = false;
                    UI_Rml_SetMenuState(pending_type, pending_paused);
                }
        })) {
            rmlui.intro_done = true;
        }
    } else if (!ui_splash || !ui_splash->integer) {
        rmlui.intro_done = true;
        debug_log("RmlUi: intro reload: startup splash disabled (ui_splash 0)\n");
    } else if (ui_rmlui_skip_intro && ui_rmlui_skip_intro->integer) {
        rmlui.intro_done = true;
        debug_log("RmlUi: intro reload skipped by ui_rmlui_skip_intro\n");
    }

    if (!rmlui.competitive_hud.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to reload Competitive HUD preset\n");
    }
    if (!rmlui.crosshair.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to reload crosshair HUD\n");
    }
    rmlui.last_crosshair_code.clear();
    rmlui.last_crosshair_hidden = true;
    rmlui.last_crosshair_mode = use_new_crosshair();
    rmlui.last_crosshair_layouts = -1;
    rmlui.last_hud_scale = -1.0f;
    rmlui.last_crosshair_motion_log_time = -1.0;
    rmlui.last_logged_health = -1;
    rmlui.last_logged_armor = -1;
    rmlui.log_next_crosshair_diag = true;
    rmlui.log_next_hud_diag = true;
    rmlui.logged_render_path = false;
    if (!rmlui.settings_menu.Init(rmlui.context)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: failed to reload settings menu\n");
    } else if (settings_menu_was_open) {
        rmlui.settings_menu.SetTransparentBackground(rmlui.menu_transparent);
        rmlui.settings_menu.ShowRoot();
    }
    if (menu_was_open && !crosshair_menu_was_open && !settings_menu_was_open) {
        UI_Rml_SetMenuState(menu_type, rmlui.menu_transparent);
    } else if (crosshair_menu_was_open || settings_menu_was_open) {
        refresh_menu_mouse_position();
    }
    refresh_title_music();
}

bool UI_Rml_OpenDocument(const char *name)
{
    if (!rmlui.context || !name || !name[0]) {
        return false;
    }

    auto existing = rmlui.documents.find(name);
    if (existing != rmlui.documents.end()) {
        existing->second->Show();
        return true;
    }

    std::string path = "ui/";
    path += name;
    path += ".rml";
    Rml::ElementDocument *doc = rmlui.context->LoadDocument(path);
    if (!doc) {
        Com_LPrintf(PRINT_ERROR, "RmlUi: failed to load %s\n", path.c_str());
        return false;
    }

    doc->Show();
    bind_commands(doc);
    rmlui.documents[name] = doc;
    if (is_menu_document_name(name)) {
        refresh_menu_mouse_position();
    }
    debug_log("RmlUi: open document %s\n", path.c_str());
    return true;
}

void UI_Rml_CloseDocument(const char *name)
{
    if (!rmlui.context || !name) {
        return;
    }
    auto it = rmlui.documents.find(name);
    if (it == rmlui.documents.end()) {
        return;
    }
    debug_log("RmlUi: close document ui/%s.rml\n", name);
    rmlui.context->UnloadDocument(it->second);
    rmlui.documents.erase(it);
}

bool UI_Rml_OpenMenuDocument(const char *name, bool push_history)
{
    if (!rmlui.initialized || !rmlui.context || !name || !name[0]) {
        return false;
    }

    if (!is_menu_document_name(name)) {
        Com_LPrintf(PRINT_WARNING, "RmlUi: unsupported menu document %s\n", name);
        return false;
    }

    if (!rmlui.active_menu_document.empty() && rmlui.active_menu_document == name) {
        return true;
    }

    if (push_history && !rmlui.active_menu_document.empty()) {
        rmlui.menu_stack.push_back(rmlui.active_menu_document);
    }

    UI_Rml_CloseDocument("main_menu");
    UI_Rml_CloseDocument("pause_menu");
    UI_Rml_CloseDocument("options");
    UI_Rml_CloseDocument("developer");
    UI_Rml_CloseDocument("video");

    if (!UI_Rml_OpenDocument(name)) {
        if (!rmlui.menu_stack.empty()) {
            rmlui.menu_stack.pop_back();
        }
        rmlui.active_menu_document.clear();
        return false;
    }

    rmlui.active_menu_document = name;
    refresh_title_music();
    debug_log("RmlUi: menu document=%s push_history=%d stack_depth=%zu\n",
              name,
              push_history ? 1 : 0,
              rmlui.menu_stack.size());
    return true;
}

void UI_Rml_Back(void)
{
    if (!rmlui.initialized || !rmlui.context) {
        return;
    }

    if (!rmlui.menu_stack.empty()) {
        std::string previous = rmlui.menu_stack.back();
        rmlui.menu_stack.pop_back();
        UI_Rml_OpenMenuDocument(previous.c_str(), false);
        return;
    }

    if (rmlui.menu_transparent) {
        UI_Rml_OpenMenuDocument("pause_menu", false);
    } else {
        UI_Rml_OpenMenuDocument("main_menu", false);
    }
}

void UI_Rml_SetHudState(const ui_rml_hud_state_t *state)
{
    if (!UI_Rml_IsEnabled() || !state) {
        return;
    }
    if (rmlui.intro.IsPlaying()) {
        CompetitiveHUDState hidden = {};
        hidden.hidden = true;
        rmlui.competitive_hud.Update(hidden);
        return;
    }

    CompetitiveHUDState competitive = {};
    competitive.health = state->health;
    competitive.armor = state->armor;
    competitive.kills = state->kills;
    competitive.ammo = state->ammo;
    competitive.ammo_max = state->ammo_max;
    competitive.ammo_reserve = state->ammo_reserve;
    competitive.hidden = (state->layouts & LAYOUTS_HIDE_HUD) != 0 || rmlui.menu_open;
    competitive.pickup_alpha = state->pickup_alpha;
    competitive.pickup = state->pickup;
    snprintf(competitive.weapon_name, sizeof(competitive.weapon_name), "%s", state->weapon_name ? state->weapon_name : "");
    snprintf(rmlui.last_weapon_name, sizeof(rmlui.last_weapon_name), "%s", state->weapon_name ? state->weapon_name : "");
    rmlui.competitive_hud.Update(competitive);
    if (debug_enabled() &&
        (competitive.health != rmlui.last_logged_health || competitive.armor != rmlui.last_logged_armor)) {
        const int health_value = std::max(0, std::min(competitive.health, 999));
        const int armor_value = std::max(0, std::min(competitive.armor, 999));
        const int combined_health = std::max(0, std::min(health_value + armor_value, 999));
        debug_log("RmlUi HUD values: health=%d armor=%d combined_health=%d hp_indicator=%d armor_indicator=%d\n",
                  health_value,
                  armor_value,
                  combined_health,
                  health_value,
                  armor_value);
        rmlui.last_logged_health = competitive.health;
        rmlui.last_logged_armor = competitive.armor;
    }
    const float hud_scale = ui_rml_hud_scale ? std::max(0.5f, std::min(ui_rml_hud_scale->value, 6.0f)) : 3.0f;
    rmlui.competitive_hud.SetScale(hud_scale);
    rmlui.competitive_hud.SetOpacity(state->hud_alpha);
    if (std::fabs(hud_scale - rmlui.last_hud_scale) >= 0.001f) {
        debug_log("RmlUi: HUD scale request=%.3f viewport=%dx%d ui_scale_cvar=%.3f hud_alpha=%.3f\n",
                  hud_scale,
                  rmlui.width,
                  rmlui.height,
                  ui_rml_hud_scale ? ui_rml_hud_scale->value : 3.0f,
                  state->hud_alpha);
        rmlui.last_hud_scale = hud_scale;
        rmlui.log_next_hud_diag = true;
        rmlui.log_next_crosshair_diag = true;
    }
}

void UI_Rml_SetMenuState(int menu_type, bool paused)
{
    if (!rmlui.initialized || !rmlui.context) {
        return;
    }
    if (rmlui.intro.IsPlaying()) {
        rmlui.pending_menu = true;
        rmlui.pending_menu_type = menu_type;
        rmlui.pending_menu_paused = paused;
        return;
    }

    UI_Rml_CloseDocument("main_menu");
    UI_Rml_CloseDocument("pause_menu");

    rmlui.menu_type = menu_type;
    rmlui.menu_open = true;
    rmlui.menu_transparent = paused;
    rmlui.active_menu_document.clear();
    rmlui.menu_stack.clear();
    debug_log("RmlUi: set menu state type=%d paused=%d\n", menu_type, paused ? 1 : 0);

    if (paused) {
        UI_Rml_OpenMenuDocument("pause_menu", false);
    } else {
        CompetitiveHUDState hidden = {};
        hidden.hidden = true;
        rmlui.competitive_hud.Update(hidden);
        rmlui.crosshair.SetHidden(true);
        UI_Rml_OpenMenuDocument("main_menu", false);
    }
    refresh_title_music();
}

void UI_Rml_UpdateCrosshair(float dt, float speed, bool on_ground, bool crouched, bool just_fired, int layouts)
{
    if (!UI_Rml_IsEnabled()) {
        return;
    }
    const bool new_crosshair = use_new_crosshair();
    if (new_crosshair != rmlui.last_crosshair_mode) {
        debug_log("RmlUi crosshair: renderer mode changed new=%d legacy=%d\n",
                  new_crosshair ? 1 : 0,
                  new_crosshair ? 0 : 1);
        rmlui.last_crosshair_mode = new_crosshair;
        rmlui.log_next_crosshair_diag = true;
    }
    if (rmlui.intro.IsPlaying()) {
        rmlui.crosshair.SetHidden(true);
        if (!rmlui.last_crosshair_hidden) {
            debug_log("RmlUi crosshair: hidden by intro\n");
            rmlui.last_crosshair_hidden = true;
            rmlui.log_next_crosshair_diag = true;
        }
        return;
    }
    if (!new_crosshair) {
        rmlui.crosshair.SetHidden(true);
        if (!rmlui.last_crosshair_hidden) {
            debug_log("RmlUi crosshair: hidden because legacy crosshair renderer is active\n");
            rmlui.last_crosshair_hidden = true;
            rmlui.log_next_crosshair_diag = true;
        }
        return;
    }

    sync_crosshair_config();
    const bool hidden = (layouts & (LAYOUTS_HIDE_HUD | LAYOUTS_HIDE_CROSSHAIR)) != 0 || rmlui.menu_open;
    if (hidden != rmlui.last_crosshair_hidden || layouts != rmlui.last_crosshair_layouts) {
        debug_log("RmlUi crosshair: visibility hidden=%d layouts=0x%x menu_open=%d dt=%.4f speed=%.1f moving=%d grounded=%d crouched=%d fired=%d\n",
                  hidden ? 1 : 0,
                  layouts,
                  rmlui.menu_open ? 1 : 0,
                  dt,
                  speed,
                  speed > rmlui.crosshair.MovementThreshold() ? 1 : 0,
                  on_ground ? 1 : 0,
                  crouched ? 1 : 0,
                  just_fired ? 1 : 0);
        rmlui.last_crosshair_hidden = hidden;
        rmlui.last_crosshair_layouts = layouts;
        rmlui.log_next_crosshair_diag = true;
    }
    rmlui.crosshair.SetHidden(hidden);
    const float previous_speed = rmlui.crosshair.LastSpeed();
    rmlui.crosshair.Update(dt, speed, on_ground, crouched, just_fired);

    if (!hidden && crosshair_motion_debug_enabled()) {
        const bool log_now = just_fired ||
            std::fabs(speed - previous_speed) > 20.0f ||
            rmlui.last_crosshair_motion_log_time < 0.0 ||
            rmlui.last_time_seconds - rmlui.last_crosshair_motion_log_time >= 0.125;
        if (log_now) {
            debug_log("RmlUi crosshair motion: dt=%.4f speed=%.1f threshold=%.1f moving=%d grounded=%d crouched=%d fired=%d move=%.2f fire=%.2f air=%.2f final=%.2f weapon=%s\n",
                      dt,
                      speed,
                      rmlui.crosshair.MovementThreshold(),
                      rmlui.crosshair.LastMoving() ? 1 : 0,
                      rmlui.crosshair.LastOnGround() ? 1 : 0,
                      rmlui.crosshair.LastCrouched() ? 1 : 0,
                      rmlui.crosshair.LastFired() ? 1 : 0,
                      rmlui.crosshair.MoveContribution(),
                      rmlui.crosshair.FireContribution(),
                      rmlui.crosshair.AirContribution(),
                      rmlui.crosshair.FinalDynamicGap(),
                      rmlui.last_weapon_name[0] ? rmlui.last_weapon_name : "<none>");
            rmlui.last_crosshair_motion_log_time = rmlui.last_time_seconds;
            if (ui_rmlui_crosshair_debug && ui_rmlui_crosshair_debug->integer > 1) {
                rmlui.log_next_crosshair_diag = true;
            }
        }
    }
}

bool UI_Rml_UseLegacyCrosshair(void)
{
    return !use_new_crosshair();
}

void UI_Rml_ToggleCrosshairMenu(void)
{
    if (!UI_Rml_IsEnabled()) {
        return;
    }
    show_settings_section(SettingsTab::Game, "Crosshair");
}

void UI_Rml_ToggleSettingsMenu(void)
{
    if (!UI_Rml_IsEnabled()) {
        return;
    }

    if (!rmlui.settings_menu.IsVisible()) {
        show_settings_root();
        return;
    }

    rmlui.settings_menu.Hide();

    if (rmlui.active_menu_document.empty() && !rmlui.crosshair_menu.IsVisible() && !rmlui.menu_stack.empty()) {
        UI_Rml_Back();
        return;
    }

    if (rmlui.active_menu_document.empty() && !rmlui.crosshair_menu.IsVisible()) {
        rmlui.menu_open = false;
        rmlui.menu_transparent = false;
        Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
    } else {
        refresh_menu_mouse_position();
    }
}

bool UI_Rml_HandleKeyEvent(int key, bool down)
{
    if (!UI_Rml_IsEnabled()) {
        return false;
    }
    if (rmlui.intro.IsPlaying() && down) {
        rmlui.intro.Skip();
        return true;
    }
    if (rmlui.settings_menu.IsVisible() && rmlui.settings_menu.HandleKeyEvent(key, down)) {
        return true;
    }

    if (key == K_ESCAPE && down && rmlui.menu_open) {
        if (rmlui.settings_menu.IsVisible()) {
            rmlui.settings_menu.Hide();
            if (rmlui.active_menu_document.empty() && !rmlui.crosshair_menu.IsVisible()) {
                rmlui.menu_open = false;
                rmlui.menu_transparent = false;
                Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
            } else {
                refresh_menu_mouse_position();
            }
            return true;
        }
        if (rmlui.crosshair_menu.IsVisible()) {
            rmlui.crosshair_menu.Hide();
            if (rmlui.active_menu_document.empty()) {
                rmlui.menu_open = false;
                rmlui.menu_transparent = false;
                Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
            } else {
                refresh_menu_mouse_position();
            }
            return true;
        }
        UI_Rml_ForceMenuOff();
        Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));
        return true;
    }

    if (key == K_MWHEELUP && down) {
        if (rmlui.menu_open && !rmlui.settings_menu.IsVisible() && !rmlui.crosshair_menu.IsVisible()) {
            if (navigate_menu_focus(-1)) {
                return true;
            }
        }
        return rmlui.context->ProcessMouseWheel(-1.0f, key_modifiers());
    }
    if (key == K_MWHEELDOWN && down) {
        if (rmlui.menu_open && !rmlui.settings_menu.IsVisible() && !rmlui.crosshair_menu.IsVisible()) {
            if (navigate_menu_focus(1)) {
                return true;
            }
        }
        return rmlui.context->ProcessMouseWheel(1.0f, key_modifiers());
    }
    if (key >= K_MOUSEFIRST && key <= K_MOUSE8) {
        return UI_Rml_HandleMouseButton(key, down);
    }

    // Arrow key navigation through menu buttons.
    if (down && !rmlui.settings_menu.IsVisible() && !rmlui.crosshair_menu.IsVisible()) {
        if (key == K_UPARROW && rmlui.menu_open) {
            return navigate_menu_focus(-1);
        }
        if (key == K_DOWNARROW && rmlui.menu_open) {
            return navigate_menu_focus(1);
        }
        // ENTER activates the focused button via a synthetic mouse click.
        if ((key == K_ENTER || key == K_SPACE) && rmlui.menu_open) {
            Rml::Element *focused = rmlui.context->GetFocusElement();
            if (focused && focused->GetTagName() == "button") {
                focused->Click();
                return true;
            }
        }
    }

    Rml::Input::KeyIdentifier mapped = map_key(key);
    if (mapped == Rml::Input::KI_UNKNOWN) {
        return false;
    }
    return down ? rmlui.context->ProcessKeyDown(mapped, key_modifiers())
                : rmlui.context->ProcessKeyUp(mapped, key_modifiers());
}

bool UI_Rml_HandleMouseMove(int x, int y)
{
    if (!UI_Rml_IsEnabled()) {
        return false;
    }
    update_mouse_position(x, y);
    debug_log("RmlUi: mouse move x=%d y=%d\n", rmlui.mouse_x, rmlui.mouse_y);
    return rmlui.context->ProcessMouseMove(rmlui.mouse_x, rmlui.mouse_y, key_modifiers());
}

bool UI_Rml_HandleMouseButton(int key, bool down)
{
    if (!UI_Rml_IsEnabled()) {
        return false;
    }
    if (rmlui.intro.IsPlaying() && down) {
        rmlui.intro.Skip();
        return true;
    }
    int button = key - K_MOUSE1;
    if (button < 0 || button > 2) {
        return false;
    }

    rmlui.context->ProcessMouseMove(rmlui.mouse_x, rmlui.mouse_y, key_modifiers());
    debug_log("RmlUi: mouse button=%d down=%d x=%d y=%d\n", button, down ? 1 : 0, rmlui.mouse_x, rmlui.mouse_y);

    if (rmlui.settings_menu.HandleMouseButton(rmlui.mouse_x, rmlui.mouse_y, down)) {
        return true;
    }

    return down ? rmlui.context->ProcessMouseButtonDown(button, key_modifiers())
                : rmlui.context->ProcessMouseButtonUp(button, key_modifiers());
}

bool UI_Rml_HandleTextInput(int key)
{
    if (!UI_Rml_IsEnabled() || key < 32 || key > 0x10ffff) {
        return false;
    }
    if (rmlui.settings_menu.IsVisible() && rmlui.settings_menu.HandleTextInput(key)) {
        return true;
    }
    return rmlui.context->ProcessTextInput(static_cast<Rml::Character>(key));
}

bool UI_Rml_IsMenuOpen(void)
{
    return UI_Rml_IsEnabled() && (rmlui.menu_open || rmlui.intro.IsPlaying());
}

bool UI_Rml_IsTransparent(void)
{
    return !UI_Rml_IsMenuOpen() || rmlui.menu_transparent;
}

void UI_Rml_ForceMenuOff(void)
{
    if (!rmlui.context) {
        return;
    }
    stop_title_music();
    UI_Rml_CloseDocument("main_menu");
    UI_Rml_CloseDocument("pause_menu");
    UI_Rml_CloseDocument("options");
    UI_Rml_CloseDocument("developer");
    UI_Rml_CloseDocument("video");
    rmlui.settings_menu.Hide();
    rmlui.crosshair_menu.Hide();
    rmlui.menu_open = false;
    rmlui.menu_transparent = false;
    rmlui.active_menu_document.clear();
    rmlui.menu_stack.clear();
}

} // extern "C"
