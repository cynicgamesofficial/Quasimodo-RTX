#include "crosshair_menu.h"

#include <algorithm>
#include <cstdio>

extern "C" {
#include "shared/shared.h"
#include "common/cvar.h"
}

#undef inline
#undef min
#undef max
#undef DotProduct
#undef CrossProduct

namespace {

int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

float clamp_float(float value, float min_value, float max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

Rml::String px(int value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%dpx", value);
    return buffer;
}

const char *style_name(CrosshairStyle style)
{
    switch (style) {
    case CrosshairStyle::Dot: return "Dot";
    case CrosshairStyle::Cross: return "Cross";
    case CrosshairStyle::Circle: return "Circle";
    case CrosshairStyle::Classic:
    default: return "Classic";
    }
}

void set_text(Rml::Element *element, const char *text)
{
    if (element) {
        element->SetInnerRML(text ? text : "");
    }
}

void set_display(Rml::Element *element, bool visible)
{
    if (element) {
        if (visible) {
            element->RemoveProperty("display");
        } else {
            element->SetProperty("display", "none");
        }
    }
}

void set_cvar_int(const char *name, int value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    Cvar_Set(name, buffer);
}

void set_cvar_float(const char *name, float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    Cvar_Set(name, buffer);
}

void push_config_cvars(const CrosshairConfig& cfg)
{
    set_cvar_int("ui_crosshair_style", static_cast<int>(cfg.style));
    set_cvar_int("ui_crosshair_dynamic", cfg.dynamic ? 1 : 0);
    set_cvar_int("ui_crosshair_dot", cfg.dot ? 1 : 0);
    set_cvar_int("ui_crosshair_outline", cfg.outline ? cfg.outline_thickness : 0);
    set_cvar_int("ui_crosshair_tstyle", cfg.t_style ? 1 : 0);
    set_cvar_int("ui_crosshair_size", cfg.size);
    set_cvar_int("ui_crosshair_thickness", cfg.thickness);
    set_cvar_int("ui_crosshair_gap", cfg.gap);
    set_cvar_float("ui_crosshair_opacity", cfg.opacity);
    set_cvar_float("ui_crosshair_red", cfg.r);
    set_cvar_float("ui_crosshair_green", cfg.g);
    set_cvar_float("ui_crosshair_blue", cfg.b);
}

} // namespace

bool CrosshairMenu::Init(Rml::Context *ctx, CrosshairHUD *crosshair)
{
    if (!ctx || !crosshair) {
        return false;
    }

    Shutdown();
    m_ctx = ctx;
    m_crosshair = crosshair;
    m_doc = ctx->LoadDocument("ui/crosshair/crosshair_menu.rml");
    if (!m_doc) {
        m_ctx = nullptr;
        m_crosshair = nullptr;
        return false;
    }

    m_doc->Show();
    m_root = m_doc->GetElementById("ch-menu");
    m_pv_t = m_doc->GetElementById("pv-line-t");
    m_pv_b = m_doc->GetElementById("pv-line-b");
    m_pv_l = m_doc->GetElementById("pv-line-l");
    m_pv_r = m_doc->GetElementById("pv-line-r");
    m_pv_dot = m_doc->GetElementById("pv-dot");
    m_pv_circle = m_doc->GetElementById("pv-circle");

    Rml::ElementList controls;
    m_doc->QuerySelectorAll(controls, "[data-ch-action]");
    for (Rml::Element *element : controls) {
        element->AddEventListener(Rml::EventId::Click, this);
    }

    SyncUIFromConfig();
    Hide();
    return true;
}

void CrosshairMenu::Show()
{
    if (!m_doc) {
        return;
    }
    m_visible = true;
    if (m_root) {
        m_root->SetAttribute("class", m_transparent_background ? "pause-root" : "menu-root");
    }
    set_display(m_root, true);
    SyncUIFromConfig();
}

void CrosshairMenu::Hide()
{
    m_visible = false;
    set_display(m_root, false);
}

void CrosshairMenu::SetTransparentBackground(bool transparent)
{
    m_transparent_background = transparent;
    if (m_root) {
        m_root->SetAttribute("class", transparent ? "pause-root" : "menu-root");
    }
}

bool CrosshairMenu::TakeCloseRequest()
{
    const bool requested = m_close_requested;
    m_close_requested = false;
    return requested;
}

void CrosshairMenu::Toggle()
{
    if (m_visible) {
        Hide();
    } else {
        Show();
    }
}

void CrosshairMenu::SetValueText(const char *id, const char *text)
{
    if (!m_doc) {
        return;
    }
    set_text(m_doc->GetElementById(id), text);
}

void CrosshairMenu::SyncUIFromConfig()
{
    if (!m_doc || !m_crosshair) {
        return;
    }

    const CrosshairConfig& cfg = m_crosshair->Config();
    char buffer[64];
    SetValueText("val-style", style_name(cfg.style));
    SetValueText("val-dynamic", cfg.dynamic ? "On" : "Off");
    SetValueText("val-dot", cfg.dot ? "On" : "Off");
    SetValueText("val-outline", cfg.outline ? "On" : "Off");
    SetValueText("val-tstyle", cfg.t_style ? "On" : "Off");
    std::snprintf(buffer, sizeof(buffer), "%d", cfg.size);
    SetValueText("val-size", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", cfg.thickness);
    SetValueText("val-thickness", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", cfg.gap);
    SetValueText("val-gap", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", cfg.outline_thickness);
    SetValueText("val-outline-size", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(cfg.opacity * 100.0f));
    SetValueText("val-opacity", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(cfg.r * 255.0f));
    SetValueText("val-r", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(cfg.g * 255.0f));
    SetValueText("val-g", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(cfg.b * 255.0f));
    SetValueText("val-b", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.1f", cfg.dyn_move_scale);
    SetValueText("val-dmove", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.1f", cfg.dyn_shoot_scale);
    SetValueText("val-dshoot", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.0f", cfg.dyn_recovery);
    SetValueText("val-drec", buffer);

    set_display(m_doc->GetElementById("ch-dyn-section"), cfg.dynamic);
    UpdatePreview();
    UpdateSwatch();
    UpdateCodeField();
}

void CrosshairMenu::SetPreviewRect(Rml::Element *element, int x, int y, int w, int h)
{
    if (!element) {
        return;
    }
    element->SetProperty("left", px(x));
    element->SetProperty("top", px(y));
    element->SetProperty("width", px(std::max(1, w)));
    element->SetProperty("height", px(std::max(1, h)));
}

void CrosshairMenu::UpdatePreview()
{
    if (!m_crosshair) {
        return;
    }

    const CrosshairConfig& cfg = m_crosshair->Config();
    const float scale = 2.5f;
    const int cx = 170;
    const int cy = 55;
    const int size = std::max(1, static_cast<int>(cfg.size * scale));
    const int thickness = std::max(1, static_cast<int>(cfg.thickness * scale));
    const int gap = cfg.style == CrosshairStyle::Cross ? 0 : std::max(0, static_cast<int>(cfg.gap * scale));

    char color[48];
    std::snprintf(color, sizeof(color), "rgba(%d,%d,%d,%.2f)",
        static_cast<int>(cfg.r * 255.0f), static_cast<int>(cfg.g * 255.0f),
        static_cast<int>(cfg.b * 255.0f), cfg.opacity);
    for (Rml::Element *element : { m_pv_t, m_pv_b, m_pv_l, m_pv_r, m_pv_dot }) {
        if (element) {
            element->SetProperty("background-color", color);
        }
    }
    if (m_pv_circle) {
        m_pv_circle->SetProperty("border-color", color);
    }

    const bool dot_only = cfg.style == CrosshairStyle::Dot;
    const bool circle = cfg.style == CrosshairStyle::Circle;
    const bool show_lines = !dot_only && !circle;
    set_display(m_pv_t, show_lines && !cfg.t_style);
    set_display(m_pv_b, show_lines);
    set_display(m_pv_l, show_lines);
    set_display(m_pv_r, show_lines);
    set_display(m_pv_dot, cfg.dot || dot_only);
    set_display(m_pv_circle, circle);

    SetPreviewRect(m_pv_t, cx - thickness / 2, cy - gap - size, thickness, size);
    SetPreviewRect(m_pv_b, cx - thickness / 2, cy + gap, thickness, size);
    SetPreviewRect(m_pv_l, cx - gap - size, cy - thickness / 2, size, thickness);
    SetPreviewRect(m_pv_r, cx + gap, cy - thickness / 2, size, thickness);

    const int dot_size = std::max(3, thickness * 2);
    SetPreviewRect(m_pv_dot, cx - dot_size / 2, cy - dot_size / 2, dot_size, dot_size);
    if (m_pv_dot) {
        m_pv_dot->SetProperty("border-radius", px((dot_size + 1) / 2));
    }

    const int radius = std::max(4, gap + size);
    SetPreviewRect(m_pv_circle, cx - radius, cy - radius, radius * 2, radius * 2);
    if (m_pv_circle) {
        m_pv_circle->SetProperty("border-width", px(thickness));
        m_pv_circle->SetProperty("border-radius", px(radius));
    }
}

void CrosshairMenu::UpdateSwatch()
{
    if (!m_doc || !m_crosshair) {
        return;
    }

    const CrosshairConfig& cfg = m_crosshair->Config();
    char color[32];
    std::snprintf(color, sizeof(color), "rgb(%d,%d,%d)",
        static_cast<int>(cfg.r * 255.0f), static_cast<int>(cfg.g * 255.0f), static_cast<int>(cfg.b * 255.0f));
    if (Rml::Element *swatch = m_doc->GetElementById("ch-colour-swatch")) {
        swatch->SetProperty("background-color", color);
    }
}

void CrosshairMenu::UpdateCodeField()
{
    if (!m_doc || !m_crosshair) {
        return;
    }

    const Rml::String code = CrosshairConfigToString(m_crosshair->Config());
    SetValueText("ch-code-text", code.c_str());
}

void CrosshairMenu::ApplyAction(const Rml::String& action)
{
    if (!m_crosshair) {
        return;
    }

    CrosshairConfig& cfg = m_crosshair->Config();
    if (action == "close") {
        Hide();
        m_close_requested = true;
        return;
    }
    if (action == "apply") {
        push_config_cvars(cfg);
        Cvar_Set("cl_crosshair_code", CrosshairConfigToString(cfg).c_str());
        SyncUIFromConfig();
        return;
    }
    if (action == "reset") {
        cfg = CrosshairConfig{};
    } else if (action == "style_prev") {
        cfg.style = static_cast<CrosshairStyle>((static_cast<int>(cfg.style) + 3) % 4);
    } else if (action == "style_next") {
        cfg.style = static_cast<CrosshairStyle>((static_cast<int>(cfg.style) + 1) % 4);
    } else if (action == "dynamic") {
        cfg.dynamic = !cfg.dynamic;
    } else if (action == "dot") {
        cfg.dot = !cfg.dot;
    } else if (action == "outline") {
        cfg.outline = !cfg.outline;
        cfg.outline_thickness = cfg.outline ? std::max(1, cfg.outline_thickness) : 0;
    } else if (action == "tstyle") {
        cfg.t_style = !cfg.t_style;
    } else if (action == "size_dec") {
        cfg.size = clamp_int(cfg.size - 1, 1, 20);
    } else if (action == "size_inc") {
        cfg.size = clamp_int(cfg.size + 1, 1, 20);
    } else if (action == "thickness_dec") {
        cfg.thickness = clamp_int(cfg.thickness - 1, 1, 5);
    } else if (action == "thickness_inc") {
        cfg.thickness = clamp_int(cfg.thickness + 1, 1, 5);
    } else if (action == "gap_dec") {
        cfg.gap = clamp_int(cfg.gap - 1, 0, 15);
    } else if (action == "gap_inc") {
        cfg.gap = clamp_int(cfg.gap + 1, 0, 15);
    } else if (action == "outline_size_dec") {
        cfg.outline_thickness = clamp_int(cfg.outline_thickness - 1, 0, 3);
        cfg.outline = cfg.outline_thickness > 0;
    } else if (action == "outline_size_inc") {
        cfg.outline_thickness = clamp_int(std::max(0, cfg.outline_thickness) + 1, 0, 3);
        cfg.outline = cfg.outline_thickness > 0;
    } else if (action == "opacity_dec") {
        cfg.opacity = clamp_float(cfg.opacity - 0.05f, 0.0f, 1.0f);
    } else if (action == "opacity_inc") {
        cfg.opacity = clamp_float(cfg.opacity + 0.05f, 0.0f, 1.0f);
    } else if (action == "r_dec") {
        cfg.r = clamp_float(cfg.r - (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "r_inc") {
        cfg.r = clamp_float(cfg.r + (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "g_dec") {
        cfg.g = clamp_float(cfg.g - (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "g_inc") {
        cfg.g = clamp_float(cfg.g + (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "b_dec") {
        cfg.b = clamp_float(cfg.b - (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "b_inc") {
        cfg.b = clamp_float(cfg.b + (16.0f / 255.0f), 0.0f, 1.0f);
    } else if (action == "dmove_dec") {
        cfg.dyn_move_scale = clamp_float(cfg.dyn_move_scale - 0.1f, 1.0f, 8.0f);
    } else if (action == "dmove_inc") {
        cfg.dyn_move_scale = clamp_float(cfg.dyn_move_scale + 0.1f, 1.0f, 8.0f);
    } else if (action == "dshoot_dec") {
        cfg.dyn_shoot_scale = clamp_float(cfg.dyn_shoot_scale - 0.1f, 1.0f, 10.0f);
    } else if (action == "dshoot_inc") {
        cfg.dyn_shoot_scale = clamp_float(cfg.dyn_shoot_scale + 0.1f, 1.0f, 10.0f);
    } else if (action == "drec_dec") {
        cfg.dyn_recovery = clamp_float(cfg.dyn_recovery - 1.0f, 1.0f, 30.0f);
    } else if (action == "drec_inc") {
        cfg.dyn_recovery = clamp_float(cfg.dyn_recovery + 1.0f, 1.0f, 30.0f);
    }

    m_crosshair->ApplyConfig();
    push_config_cvars(cfg);
    SyncUIFromConfig();
}

void CrosshairMenu::ProcessEvent(Rml::Event& event)
{
    if (!(event == Rml::EventId::Click)) {
        return;
    }

    Rml::Element *element = event.GetCurrentElement();
    if (!element) {
        return;
    }

    ApplyAction(element->GetAttribute<Rml::String>("data-ch-action", ""));
}

void CrosshairMenu::Shutdown()
{
    if (m_doc) {
        m_doc->Close();
    }
    m_ctx = nullptr;
    m_doc = nullptr;
    m_root = nullptr;
    m_crosshair = nullptr;
    m_visible = false;
    m_transparent_background = false;
    m_pv_t = nullptr;
    m_pv_b = nullptr;
    m_pv_l = nullptr;
    m_pv_r = nullptr;
    m_pv_dot = nullptr;
    m_pv_circle = nullptr;
}
