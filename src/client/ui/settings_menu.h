#pragma once

#include <RmlUi/Core.h>

#include <string>
#include <vector>

enum class SettingsTab {
    Game = 0,
    Video,
    RTX,
    Environment,
    Effects,
    Developer,
    Audio,
    Controls,
    Crosshair,
    COUNT,
    Graphics = Video,
    Network = Game,
    Advanced = Developer
};

class SettingsMenu : public Rml::EventListener {
public:
    struct Option {
        const char *label;
        const char *value;
    };

    enum class Kind {
        Section,
        Toggle,
        Slider,
        Dropdown,
        Text,
        Bitmask,
        Keybind,
        Link
    };

    struct SettingDef {
        SettingsTab tab;
        Kind kind;
        const char *section;
        const char *label;
        const char *cvar;
        const char *default_value;
        float min_value;
        float max_value;
        float step;
        int decimals;
        bool inverted;
        int bit;
        const Option *options;
        int option_count;
        const char *command;
        const char *description;
    };

    bool Init(Rml::Context *ctx);
    void Show(SettingsTab tab = SettingsTab::Game);
    void ShowSection(SettingsTab tab, const char *section);
    void ShowRoot();
    void Hide();
    void SetTransparentBackground(bool transparent);
    bool IsVisible() const { return m_visible; }
    void Toggle();
    void Shutdown();
    bool TakeCloseRequest();
    bool HandleKeyEvent(int key, bool down);
    bool HandleTextInput(int key);
    bool HandleMouseButton(int x, int y, bool down);
    void ProcessEvent(Rml::Event& event) override;

private:
    struct Binding {
        const SettingDef *def;
        Rml::Element *row = nullptr;
        Rml::Element *label = nullptr;
        Rml::Element *description = nullptr;
        Rml::Element *control = nullptr;
        Rml::Element *display = nullptr;
        Rml::Element *slider_wrap = nullptr;
        Rml::Element *fill = nullptr;
        Rml::Element *thumb = nullptr;
        Rml::Element *value = nullptr;
        std::string pending;
        bool available = true;
    };

    void BuildTabContent(SettingsTab tab);
    void BuildSectionMenu(SettingsTab tab);
    void BuildSectionContent(SettingsTab tab, const std::string& section);
    void SwitchTab(SettingsTab tab);
    void SwitchSection(const std::string& section);
    void SyncAllFromCvars();
    void SyncBinding(Binding& binding);
    void SyncDirtyState();
    void Apply();
    void ResetDefaults(bool all);
    void RequestBack();
    void StartRebind(Binding& binding);
    void FinishRebind(int key);
    void CancelRebind();
    void StartTextEdit(Binding& binding);
    void FinishTextEdit(bool cancel);
    void Activate(Binding& binding, const char *action);
    void FocusBinding(int index, bool scroll_into_view = false);
    void AdjustFocused(int dir);
    bool ScrollContent(float pixels);
    void FlushConfig();
    float SliderValueFromMouse(const Binding& binding, float mouse_x) const;
    void SetSliderFromMouse(Binding& binding, float mouse_x, const char *source);

    Rml::Element *AddSection(Rml::Element *parent, const char *label);
    Binding& AddSetting(Rml::Element *parent, const SettingDef& def);

    const SettingDef *FindDefByAction(const std::string& action) const;
    Binding *FindBinding(const std::string& action);
    std::string CvarString(const SettingDef& def) const;
    int OptionIndex(const SettingDef& def, const std::string& value) const;
    std::string FormatValue(const SettingDef& def, const std::string& value) const;
    void SetPending(Binding& binding, const std::string& value);

    Rml::Context *m_ctx = nullptr;
    Rml::ElementDocument *m_doc = nullptr;
    Rml::Element *m_root = nullptr;
    Rml::Element *m_shell = nullptr;
    Rml::Element *m_home = nullptr;
    Rml::Element *m_panel = nullptr;
    Rml::Element *m_content = nullptr;
    Rml::Element *m_breadcrumb = nullptr;
    Rml::Element *m_title = nullptr;
    Rml::Element *m_dirty = nullptr;
    Rml::Element *m_confirm = nullptr;
    Rml::Element *m_reset = nullptr;
    Rml::Element *m_back = nullptr;
    SettingsTab m_active_tab = SettingsTab::Game;
    bool m_showing_root = true;
    bool m_showing_section_menu = false;
    bool m_visible = false;
    bool m_transparent_background = false;
    bool m_dirty_state = false;
    bool m_close_requested = false;
    bool m_needs_config_write = false;
    Rml::Element *m_tab_btns[(int)SettingsTab::COUNT] = {};
    std::vector<Rml::Element *> m_section_btns;
    std::vector<std::string> m_section_names;
    std::vector<Binding> m_bindings;
    std::string m_active_section;
    int m_focus_index = -1;
    int m_rebind_index = -1;
    int m_text_index = -1;
    int m_drag_slider_index = -1;
    int m_text_cursor = -1;
    std::string m_text_original;
};
