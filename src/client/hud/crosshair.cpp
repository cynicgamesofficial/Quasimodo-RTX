#include "crosshair.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
void Com_LPrintf(int type, const char *fmt, ...);
}

#define PRINT_DEVELOPER 2

namespace {

int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

float clamp_float(float value, float min_value, float max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

float saturate(float value)
{
    return clamp_float(value, 0.0f, 1.0f);
}

float smooth_step(float value)
{
    value = saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

float exp_factor(float rate, float dt)
{
    return 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
}

float approach_exp(float value, float target, float rate, float dt)
{
    return value + (target - value) * exp_factor(rate, dt);
}

constexpr float MOVE_START_SPEED = 45.0f;
constexpr float MOVE_FULL_SPEED = 240.0f;
constexpr float MOVE_ATTACK_RATE = 28.0f;
constexpr float MOVE_RELEASE_RATE = 18.0f;
constexpr float FIRE_DECAY_RATE = 30.0f;
constexpr float AIR_ATTACK_RATE = 20.0f;
constexpr float AIR_RELEASE_RATE = 14.0f;
constexpr float FIRE_SCALE_TO_GAP = 0.12f;
constexpr float FIRE_BASE_FRACTION = 0.18f;
constexpr float FIRE_MAX_FRACTION = 0.95f;
constexpr float AIR_MAX_FRACTION = 1.15f;
constexpr float FINAL_DYNAMIC_LIMIT = 32.0f;

Rml::String px(int value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%dpx", value);
    return buffer;
}

Rml::String px_float(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3fpx", value);
    return buffer;
}

void set_display(Rml::Element *element, bool visible)
{
    if (element) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

void set_color(Rml::Element *element, const char *property, const char *value)
{
    if (element) {
        element->SetProperty(property, value);
    }
}

Rml::String hex_color(float r, float g, float b, float a)
{
    const int ri = clamp_int(static_cast<int>(std::round(clamp_float(r, 0.0f, 1.0f) * 255.0f)), 0, 255);
    const int gi = clamp_int(static_cast<int>(std::round(clamp_float(g, 0.0f, 1.0f) * 255.0f)), 0, 255);
    const int bi = clamp_int(static_cast<int>(std::round(clamp_float(b, 0.0f, 1.0f) * 255.0f)), 0, 255);
    const int ai = clamp_int(static_cast<int>(std::round(clamp_float(a, 0.0f, 1.0f) * 255.0f)), 0, 255);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", ri, gi, bi, ai);
    return buffer;
}

Rml::String property_string(Rml::Element *element, const char *property)
{
    if (!element) {
        return "<missing>";
    }
    if (const Rml::Property *prop = element->GetProperty(property)) {
        return prop->ToString();
    }
    return "<unset>";
}

Rml::String border_color_string(Rml::Element *element)
{
    Rml::String border = property_string(element, "border-color");
    if (border == "<unset>") {
        border = property_string(element, "border-top-color");
    }
    return border;
}

void log_element_box(const char *name, Rml::Element *element)
{
    if (!element) {
        Com_LPrintf(PRINT_DEVELOPER, "RmlUi crosshair: %s missing\n", name);
        return;
    }

    const Rml::Vector2f pos = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::String display = property_string(element, "display");
    const Rml::String visibility = property_string(element, "visibility");
    const Rml::String opacity = property_string(element, "opacity");
    const Rml::String position = property_string(element, "position");
    const Rml::String left = property_string(element, "left");
    const Rml::String top = property_string(element, "top");
    const Rml::String width = property_string(element, "width");
    const Rml::String height = property_string(element, "height");
    const Rml::String color = property_string(element, "color");
    const Rml::String background = property_string(element, "background-color");
    const Rml::String border = border_color_string(element);
    const float center_x = pos.x + size.x * 0.5f;
    const float center_y = pos.y + size.y * 0.5f;
    Com_LPrintf(PRINT_DEVELOPER,
        "RmlUi crosshair: %s display=%s visibility=%s opacity=%s position=%s left=%s top=%s width=%s height=%s z=%.1f visible=%d box=(%.1f,%.1f %.1fx%.1f) center=(%.1f,%.1f) color=%s bg=%s border=%s\n",
        name,
        display.c_str(),
        visibility.c_str(),
        opacity.c_str(),
        position.c_str(),
        left.c_str(),
        top.c_str(),
        width.c_str(),
        height.c_str(),
        element->GetZIndex(),
        element->IsVisible(true) ? 1 : 0,
        pos.x,
        pos.y,
        size.x,
        size.y,
        center_x,
        center_y,
        color.c_str(),
        background.c_str(),
        border.c_str());
}

} // namespace

Rml::String CrosshairConfigToString(const CrosshairConfig& c)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
        static_cast<int>(c.style), c.dynamic ? 1 : 0, c.dot ? 1 : 0, c.outline ? 1 : 0, c.t_style ? 1 : 0,
        c.size, c.thickness, c.gap, c.outline_thickness,
        static_cast<int>(clamp_float(c.opacity, 0.0f, 1.0f) * 100.0f),
        static_cast<int>(clamp_float(c.r, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(clamp_float(c.g, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(clamp_float(c.b, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(c.dyn_move_scale * 10.0f),
        static_cast<int>(c.dyn_shoot_scale * 10.0f),
        static_cast<int>(c.dyn_recovery));
    return buffer;
}

bool CrosshairConfigFromString(const Rml::String& text, CrosshairConfig& out)
{
    int values[16] = {};
    const int count = std::sscanf(text.c_str(), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
        &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7],
        &values[8], &values[9], &values[10], &values[11], &values[12], &values[13], &values[14], &values[15]);
    if (count < 13) {
        return false;
    }

    out.style = static_cast<CrosshairStyle>(clamp_int(values[0], 0, 3));
    out.dynamic = values[1] != 0;
    out.dot = values[2] != 0;
    out.outline = values[3] != 0;
    out.t_style = values[4] != 0;
    out.size = clamp_int(values[5], 1, 20);
    out.thickness = clamp_int(values[6], 1, 5);
    out.gap = clamp_int(values[7], 0, 15);
    out.outline_thickness = clamp_int(values[8], 0, 3);
    out.opacity = clamp_float(values[9] / 100.0f, 0.0f, 1.0f);
    out.r = clamp_float(values[10] / 255.0f, 0.0f, 1.0f);
    out.g = clamp_float(values[11] / 255.0f, 0.0f, 1.0f);
    out.b = clamp_float(values[12] / 255.0f, 0.0f, 1.0f);
    if (count >= 16) {
        out.dyn_move_scale = clamp_float(values[13] / 10.0f, 1.0f, 8.0f);
        out.dyn_shoot_scale = clamp_float(values[14] / 10.0f, 1.0f, 10.0f);
        out.dyn_recovery = clamp_float(static_cast<float>(values[15]), 1.0f, 30.0f);
    }
    return true;
}

bool CrosshairHUD::Init(Rml::Context *ctx)
{
    if (!ctx) {
        return false;
    }

    Shutdown();
    m_ctx = ctx;
    m_doc = ctx->LoadDocument("ui/crosshair/crosshair.rml");
    if (!m_doc) {
        m_ctx = nullptr;
        return false;
    }

    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    m_root = m_doc->GetElementById("ch-root");
    m_debug_center = m_doc->GetElementById("ch-debug-center");
    m_debug_label = m_doc->GetElementById("ch-debug-label");
    m_outline_t = m_doc->GetElementById("ch-outline-line-t");
    m_outline_b = m_doc->GetElementById("ch-outline-line-b");
    m_outline_l = m_doc->GetElementById("ch-outline-line-l");
    m_outline_r = m_doc->GetElementById("ch-outline-line-r");
    m_outline_dot = m_doc->GetElementById("ch-outline-dot");
    m_outline_circle = m_doc->GetElementById("ch-outline-circle");
    m_line_t = m_doc->GetElementById("ch-line-t");
    m_line_b = m_doc->GetElementById("ch-line-b");
    m_line_l = m_doc->GetElementById("ch-line-l");
    m_line_r = m_doc->GetElementById("ch-line-r");
    m_dot = m_doc->GetElementById("ch-dot");
    m_circle = m_doc->GetElementById("ch-circle");

    if (!m_root || !m_outline_t || !m_outline_b || !m_outline_l || !m_outline_r ||
        !m_outline_dot || !m_outline_circle ||
        !m_line_t || !m_line_b || !m_line_l || !m_line_r || !m_dot || !m_circle) {
        Shutdown();
        return false;
    }

    m_hidden = true;
    SetHidden(true);
    ApplyConfig();
    return true;
}

void CrosshairHUD::ApplyConfig()
{
    if (!m_doc) {
        return;
    }

    m_cfg.size = clamp_int(m_cfg.size, 1, 20);
    m_cfg.thickness = clamp_int(m_cfg.thickness, 1, 5);
    m_cfg.gap = clamp_int(m_cfg.gap, 0, 15);
    m_cfg.outline_thickness = clamp_int(m_cfg.outline ? m_cfg.outline_thickness : 0, 0, 3);
    m_cfg.outline = m_cfg.outline_thickness > 0;
    m_cfg.opacity = clamp_float(m_cfg.opacity, 0.0f, 1.0f);
    m_cfg.r = clamp_float(m_cfg.r, 0.0f, 1.0f);
    m_cfg.g = clamp_float(m_cfg.g, 0.0f, 1.0f);
    m_cfg.b = clamp_float(m_cfg.b, 0.0f, 1.0f);
    m_cfg.dyn_move_scale = clamp_float(m_cfg.dyn_move_scale, 1.0f, 8.0f);
    m_cfg.dyn_shoot_scale = clamp_float(m_cfg.dyn_shoot_scale, 1.0f, 10.0f);
    m_cfg.dyn_recovery = clamp_float(m_cfg.dyn_recovery, 1.0f, 30.0f);
    if (!m_cfg.dynamic) {
        ResetDynamic();
    }

    const Rml::String color = hex_color(m_cfg.r, m_cfg.g, m_cfg.b, m_cfg.opacity);

    for (Rml::Element *element : { m_line_t, m_line_b, m_line_l, m_line_r, m_dot }) {
        set_color(element, "background-color", color.c_str());
    }
    set_color(m_circle, "border-color", color.c_str());

    const bool show_outline = m_cfg.outline && m_cfg.outline_thickness > 0;
    for (Rml::Element *element : { m_outline_t, m_outline_b, m_outline_l, m_outline_r, m_outline_dot }) {
        set_color(element, "background-color", "#000000BF");
        set_display(element, show_outline);
    }
    set_color(m_outline_circle, "border-color", "#000000BF");

    const bool dot_only = m_cfg.style == CrosshairStyle::Dot;
    const bool circle = m_cfg.style == CrosshairStyle::Circle;
    const bool show_lines = !dot_only && !circle;
    set_display(m_line_t, show_lines && !m_cfg.t_style);
    set_display(m_line_b, show_lines);
    set_display(m_line_l, show_lines);
    set_display(m_line_r, show_lines);
    set_display(m_dot, m_cfg.dot || dot_only);
    set_display(m_circle, circle);
    set_display(m_outline_t, show_outline && show_lines && !m_cfg.t_style);
    set_display(m_outline_b, show_outline && show_lines);
    set_display(m_outline_l, show_outline && show_lines);
    set_display(m_outline_r, show_outline && show_lines);
    set_display(m_outline_dot, show_outline && (m_cfg.dot || dot_only));
    set_display(m_outline_circle, show_outline && circle);

    RebuildDOM();
}

void CrosshairHUD::SetHidden(bool hidden)
{
    m_hidden = hidden;
    if (hidden) {
        ResetDynamic();
    }
    if (m_doc) {
        if (!hidden) {
            m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        }
        m_doc->SetClass("hidden", hidden);
    }
}

void CrosshairHUD::SetDebugBounds(bool enabled)
{
    m_debug_bounds = enabled;
    if (m_doc) {
        m_doc->SetClass("debug-bounds", enabled);
    }
}

void CrosshairHUD::BringToFront()
{
    if (!m_ctx || !m_doc || m_hidden) {
        return;
    }
    m_ctx->PullDocumentToFront(m_doc);
}

float CrosshairHUD::MovementThreshold() const
{
    return MOVE_START_SPEED;
}

void CrosshairHUD::ResetDynamic()
{
    m_move_gap = 0.0f;
    m_fire_gap = 0.0f;
    m_air_gap = 0.0f;
    m_dynamic_gap = 0.0f;
    m_last_speed = 0.0f;
    m_last_moving = false;
    m_last_on_ground = true;
    m_last_crouched = false;
    m_last_fired = false;
}

void CrosshairHUD::UpdateDynamic(float dt, float speed, bool on_ground, bool crouched, bool fired)
{
    if (!m_cfg.dynamic) {
        ResetDynamic();
        return;
    }

    dt = clamp_float(dt, 0.0f, 0.1f);
    speed = std::max(0.0f, speed);
    const float base_gap = static_cast<float>(std::max(1, m_cfg.gap));
    const float release_scale = clamp_float(m_cfg.dyn_recovery / 8.0f, 0.5f, 3.0f);
    const bool moving = speed > MOVE_START_SPEED;
    const float move_t = smooth_step((speed - MOVE_START_SPEED) / (MOVE_FULL_SPEED - MOVE_START_SPEED));
    const float crouch_factor = crouched ? 0.55f : 1.0f;
    const float move_max = base_gap * m_cfg.dyn_move_scale;
    const float move_target = move_max * move_t * crouch_factor;
    const float move_rate = move_target > m_move_gap ? MOVE_ATTACK_RATE : MOVE_RELEASE_RATE * release_scale;
    m_move_gap = approach_exp(m_move_gap, move_target, move_rate, dt);

    m_fire_gap *= std::exp(-(FIRE_DECAY_RATE * release_scale) * dt);
    if (fired) {
        const float fire_max = base_gap * FIRE_MAX_FRACTION;
        const float fire_impulse = base_gap * (FIRE_BASE_FRACTION + m_cfg.dyn_shoot_scale * FIRE_SCALE_TO_GAP);
        m_fire_gap = std::min(fire_max, m_fire_gap + fire_impulse);
    }

    const float air_target = on_ground ? 0.0f : base_gap * AIR_MAX_FRACTION;
    const float air_rate = air_target > m_air_gap ? AIR_ATTACK_RATE : AIR_RELEASE_RATE * release_scale;
    m_air_gap = approach_exp(m_air_gap, air_target, air_rate, dt);

    const float final_max = std::min(FINAL_DYNAMIC_LIMIT, std::max(base_gap * 3.0f, base_gap + 4.0f));
    m_dynamic_gap = clamp_float(m_move_gap + m_fire_gap + m_air_gap, 0.0f, final_max);
    m_last_speed = speed;
    m_last_moving = moving;
    m_last_on_ground = on_ground;
    m_last_crouched = crouched;
    m_last_fired = fired;
}

void CrosshairHUD::Update(float dt, float speed, bool on_ground, bool crouched, bool just_fired)
{
    if (!m_doc || m_hidden) {
        return;
    }

    UpdateDynamic(dt, speed, on_ground, crouched, just_fired);
    RebuildDOM();
}

void CrosshairHUD::SetRect(Rml::Element *element, float x, float y, float w, float h)
{
    if (!element) {
        return;
    }
    element->SetProperty("left", px_float(x));
    element->SetProperty("top", px_float(y));
    element->SetProperty("width", px_float(std::max(1.0f, w)));
    element->SetProperty("height", px_float(std::max(1.0f, h)));
}

void CrosshairHUD::RebuildDOM()
{
    if (!m_ctx) {
        return;
    }

    const Rml::Vector2i dimensions = m_ctx->GetDimensions();
    const float cx = static_cast<float>(dimensions.x) * 0.5f;
    const float cy = static_cast<float>(dimensions.y) * 0.5f;
    const int size = std::max(1, m_cfg.size);
    const int thickness = std::max(1, m_cfg.thickness);
    const int outline_px = m_cfg.outline ? std::max(0, m_cfg.outline_thickness) : 0;
    const int outline_thickness = thickness + outline_px * 2;
    const float base_gap = m_cfg.style == CrosshairStyle::Cross ? 0.0f : static_cast<float>(m_cfg.gap);
    const float dynamic_gap = m_cfg.dynamic ? m_dynamic_gap : 0.0f;
    const int gap = std::max(0, static_cast<int>(std::round(base_gap + dynamic_gap)));

    SetRect(m_outline_t, cx - static_cast<float>(outline_thickness) * 0.5f, cy - gap - size - outline_px, outline_thickness, size + outline_px * 2);
    SetRect(m_outline_b, cx - static_cast<float>(outline_thickness) * 0.5f, cy + gap - outline_px, outline_thickness, size + outline_px * 2);
    SetRect(m_outline_l, cx - gap - size - outline_px, cy - static_cast<float>(outline_thickness) * 0.5f, size + outline_px * 2, outline_thickness);
    SetRect(m_outline_r, cx + gap - outline_px, cy - static_cast<float>(outline_thickness) * 0.5f, size + outline_px * 2, outline_thickness);

    SetRect(m_line_t, cx - static_cast<float>(thickness) * 0.5f, cy - gap - size, thickness, size);
    SetRect(m_line_b, cx - static_cast<float>(thickness) * 0.5f, cy + gap, thickness, size);
    SetRect(m_line_l, cx - gap - size, cy - static_cast<float>(thickness) * 0.5f, size, thickness);
    SetRect(m_line_r, cx + gap, cy - static_cast<float>(thickness) * 0.5f, size, thickness);

    const int dot_size = std::max(2, std::max(2, m_cfg.thickness * 2));
    const int outline_dot_size = dot_size + outline_px * 2;
    SetRect(m_outline_dot, cx - static_cast<float>(outline_dot_size) * 0.5f, cy - static_cast<float>(outline_dot_size) * 0.5f, outline_dot_size, outline_dot_size);
    m_outline_dot->SetProperty("border-radius", px((outline_dot_size + 1) / 2));
    SetRect(m_dot, cx - static_cast<float>(dot_size) * 0.5f, cy - static_cast<float>(dot_size) * 0.5f, dot_size, dot_size);
    m_dot->SetProperty("border-radius", px((dot_size + 1) / 2));

    const int radius = std::max(2, gap + size);
    const int outline_radius = radius + outline_px;
    SetRect(m_outline_circle, cx - outline_radius, cy - outline_radius, outline_radius * 2, outline_radius * 2);
    m_outline_circle->SetProperty("border-width", px(thickness + outline_px * 2));
    m_outline_circle->SetProperty("border-radius", px(outline_radius));
    SetRect(m_circle, cx - radius, cy - radius, radius * 2, radius * 2);
    m_circle->SetProperty("border-width", px(thickness));
    m_circle->SetProperty("border-radius", px(radius));

    if (m_debug_bounds) {
        SetRect(m_debug_center, cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
        if (m_debug_label) {
            m_debug_label->SetProperty("left", px_float(cx + 12.0f));
            m_debug_label->SetProperty("top", px_float(cy + 12.0f));
            m_debug_label->SetProperty("margin-left", "0px");
            m_debug_label->SetProperty("margin-top", "0px");
        }
    }
}

void CrosshairHUD::DebugDescribe(bool enabled, const char *reason)
{
    if (!enabled) {
        return;
    }

    if (!m_doc || !m_ctx) {
        Com_LPrintf(PRINT_DEVELOPER,
            "RmlUi crosshair: %s doc=%p ctx=%p initialized=0 hidden=%d\n",
            reason ? reason : "diagnostic",
            static_cast<void *>(m_doc),
            static_cast<void *>(m_ctx),
            m_hidden ? 1 : 0);
        return;
    }

    const Rml::Vector2i dimensions = m_ctx->GetDimensions();
    Com_LPrintf(PRINT_DEVELOPER,
        "RmlUi crosshair: %s doc=%p ctx=%p source=%s shown=%d hidden=%d debug_bounds=%d viewport=%dx%d size=%d thickness=%d gap=%d outline_px=%d speed=%.1f threshold=%.1f moving=%d grounded=%d crouched=%d fired=%d move=%.2f fire=%.2f air=%.2f final=%.2f root=%p line_t=%p line_b=%p line_l=%p line_r=%p dot=%p circle=%p\n",
        reason ? reason : "diagnostic",
        static_cast<void *>(m_doc),
        static_cast<void *>(m_ctx),
        m_doc->GetSourceURL().c_str(),
        m_doc->IsVisible(true) ? 1 : 0,
        m_hidden ? 1 : 0,
        m_debug_bounds ? 1 : 0,
        dimensions.x,
        dimensions.y,
        m_cfg.size,
        m_cfg.thickness,
        m_cfg.gap,
        m_cfg.outline ? m_cfg.outline_thickness : 0,
        m_last_speed,
        MovementThreshold(),
        m_last_moving ? 1 : 0,
        m_last_on_ground ? 1 : 0,
        m_last_crouched ? 1 : 0,
        m_last_fired ? 1 : 0,
        m_move_gap,
        m_fire_gap,
        m_air_gap,
        m_dynamic_gap,
        static_cast<void *>(m_root),
        static_cast<void *>(m_line_t),
        static_cast<void *>(m_line_b),
        static_cast<void *>(m_line_l),
        static_cast<void *>(m_line_r),
        static_cast<void *>(m_dot),
        static_cast<void *>(m_circle));

    log_element_box("document", m_doc);
    log_element_box("root", m_root);
    log_element_box("outline_t", m_outline_t);
    log_element_box("outline_b", m_outline_b);
    log_element_box("outline_l", m_outline_l);
    log_element_box("outline_r", m_outline_r);
    log_element_box("outline_dot", m_outline_dot);
    log_element_box("outline_circle", m_outline_circle);
    log_element_box("line_t", m_line_t);
    log_element_box("line_b", m_line_b);
    log_element_box("line_l", m_line_l);
    log_element_box("line_r", m_line_r);
    log_element_box("dot", m_dot);
    log_element_box("circle", m_circle);
}

void CrosshairHUD::Shutdown()
{
    if (m_doc) {
        m_doc->Close();
    }
    m_ctx = nullptr;
    m_doc = nullptr;
    m_root = nullptr;
    m_debug_center = nullptr;
    m_debug_label = nullptr;
    m_outline_t = nullptr;
    m_outline_b = nullptr;
    m_outline_l = nullptr;
    m_outline_r = nullptr;
    m_outline_dot = nullptr;
    m_outline_circle = nullptr;
    m_line_t = nullptr;
    m_line_b = nullptr;
    m_line_l = nullptr;
    m_line_r = nullptr;
    m_dot = nullptr;
    m_circle = nullptr;
    ResetDynamic();
    m_hidden = true;
    m_debug_bounds = false;
}
