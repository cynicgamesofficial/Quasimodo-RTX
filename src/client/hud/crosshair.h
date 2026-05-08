#pragma once

#include <RmlUi/Core.h>

enum class CrosshairStyle {
    Classic = 0,
    Dot = 1,
    Cross = 2,
    Circle = 3,
};

struct CrosshairConfig {
    CrosshairStyle style = CrosshairStyle::Classic;
    bool dynamic = false;
    bool dot = true;
    bool outline = false;
    bool t_style = false;
    int size = 5;
    int thickness = 1;
    int gap = 3;
    int outline_thickness = 0;
    float opacity = 0.85f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float dyn_move_scale = 2.0f;
    float dyn_shoot_scale = 4.0f;
    float dyn_recovery = 8.0f;
};

Rml::String CrosshairConfigToString(const CrosshairConfig& config);
bool CrosshairConfigFromString(const Rml::String& text, CrosshairConfig& out);

class CrosshairHUD {
public:
    bool Init(Rml::Context *ctx);
    void Update(float dt, float speed, bool on_ground, bool crouched, bool just_fired);
    void Shutdown();

    CrosshairConfig& Config() { return m_cfg; }
    const CrosshairConfig& Config() const { return m_cfg; }
    void ApplyConfig();
    void SetHidden(bool hidden);
    void SetDebugBounds(bool enabled);
    void BringToFront();
    void DebugDescribe(bool enabled, const char *reason);
    bool IsHidden() const { return m_hidden; }
    bool IsInitialized() const { return m_doc != nullptr; }
    float MoveContribution() const { return m_move_gap; }
    float FireContribution() const { return m_fire_gap; }
    float AirContribution() const { return m_air_gap; }
    float FinalDynamicGap() const { return m_dynamic_gap; }
    float LastSpeed() const { return m_last_speed; }
    float MovementThreshold() const;
    bool LastMoving() const { return m_last_moving; }
    bool LastOnGround() const { return m_last_on_ground; }
    bool LastCrouched() const { return m_last_crouched; }
    bool LastFired() const { return m_last_fired; }

private:
    void RebuildDOM();
    void UpdateDynamic(float dt, float speed, bool on_ground, bool crouched, bool fired);
    void ResetDynamic();
    void SetRect(Rml::Element *element, float x, float y, float w, float h);

    Rml::Context *m_ctx = nullptr;
    Rml::ElementDocument *m_doc = nullptr;
    Rml::Element *m_root = nullptr;
    Rml::Element *m_debug_center = nullptr;
    Rml::Element *m_debug_label = nullptr;
    Rml::Element *m_outline_t = nullptr;
    Rml::Element *m_outline_b = nullptr;
    Rml::Element *m_outline_l = nullptr;
    Rml::Element *m_outline_r = nullptr;
    Rml::Element *m_outline_dot = nullptr;
    Rml::Element *m_outline_circle = nullptr;
    Rml::Element *m_line_t = nullptr;
    Rml::Element *m_line_b = nullptr;
    Rml::Element *m_line_l = nullptr;
    Rml::Element *m_line_r = nullptr;
    Rml::Element *m_dot = nullptr;
    Rml::Element *m_circle = nullptr;

    CrosshairConfig m_cfg;
    float m_move_gap = 0.0f;
    float m_fire_gap = 0.0f;
    float m_air_gap = 0.0f;
    float m_dynamic_gap = 0.0f;
    float m_last_speed = 0.0f;
    bool m_last_moving = false;
    bool m_last_on_ground = true;
    bool m_last_crouched = false;
    bool m_last_fired = false;
    bool m_hidden = true;
    bool m_debug_bounds = false;
};
