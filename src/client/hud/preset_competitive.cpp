#include "preset_competitive.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <string>

extern "C" {
void Com_LPrintf(int type, const char *fmt, ...);
}

#define PRINT_DEVELOPER 2

namespace {

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void SetWidth(Rml::Element *element, float percent)
{
    if (!element) {
        return;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", clamp_float(percent, 0.0f, 100.0f));
    element->SetProperty("width", buffer);
}

void SetInnerText(Rml::Element *element, const char *text)
{
    if (element) {
        element->SetInnerRML(text ? text : "");
    }
}

void SetPx(Rml::Element *element, const char *property, float value)
{
    if (!element) {
        return;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1fpx", value);
    element->SetProperty(property, buffer);
}

void SetScaledPx(Rml::Element *element, const char *property, float base, float scale)
{
    SetPx(element, property, std::max(1.0f, base * scale));
}

void SetScaledFont(Rml::Element *element, float base, float scale)
{
    SetPx(element, "font-size", std::max(1.0f, base * scale));
}

void SetRect(Rml::Element *element, float x, float y, float w, float h)
{
    if (!element) {
        return;
    }

    SetPx(element, "left", x);
    SetPx(element, "top", y);
    SetPx(element, "width", std::max(1.0f, w));
    SetPx(element, "height", std::max(1.0f, h));
}

constexpr float HUD_BASE_BOTTOM_PAD = 24.0f;
constexpr float HUD_BASE_HEIGHT = 100.0f;
constexpr float HUD_BASE_HP_WIDTH = 220.0f;
constexpr float HUD_BASE_AMMO_WIDTH = 390.0f;
constexpr float HUD_BASE_SIDE_SLOT_WIDTH = 390.0f;
constexpr float HUD_BASE_STRIPE_WIDTH = 1.0f;
constexpr float HUD_BASE_STRIPE_GAP = 26.0f;
constexpr float HUD_BASE_STRIPE_HEIGHT = 62.0f;
constexpr float HUD_BASE_KILL_WIDTH = 68.0f;
constexpr float HUD_BASE_KILL_BLOCK_HEIGHT = 86.0f;
constexpr float HUD_BASE_SIDE_BLOCK_HEIGHT = 62.0f;

float CompetitiveBaseWidth()
{
    return HUD_BASE_SIDE_SLOT_WIDTH * 2.0f +
           (HUD_BASE_STRIPE_GAP * 2.0f + HUD_BASE_STRIPE_WIDTH) * 2.0f +
           HUD_BASE_KILL_WIDTH;
}

std::string EscapeRmlText(const char *text)
{
    std::string escaped;
    if (!text) {
        return escaped;
    }

    for (const char *ch = text; *ch; ++ch) {
        switch (*ch) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#39;"; break;
        case '\n': escaped += "<br/>"; break;
        case '\r': break;
        default: escaped.push_back(*ch); break;
        }
    }

    return escaped;
}

std::string NormalizeWeaponName(const char *weapon)
{
    std::string normalized;
    if (!weapon) {
        return normalized;
    }

    for (const unsigned char ch : std::string(weapon)) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }

    return normalized;
}

std::string GetKillRingMarkup(int kills)
{
    constexpr float pi = 3.14159265358979323846f;
    const float fraction = clamp_float(static_cast<float>(kills) / static_cast<float>(CompetitiveHUD::MAX_KILLS_RING), 0.0f, 1.0f);

    int red;
    int green;
    int blue;
    if (fraction < 0.6f) {
        const float t = fraction / 0.6f;
        red = static_cast<int>(std::round(74.0f + (250.0f - 74.0f) * t));
        green = static_cast<int>(std::round(222.0f + (204.0f - 222.0f) * t));
        blue = static_cast<int>(std::round(128.0f + (21.0f - 128.0f) * t));
    } else {
        const float t = (fraction - 0.6f) / 0.4f;
        red = static_cast<int>(std::round(250.0f + (248.0f - 250.0f) * t));
        green = static_cast<int>(std::round(204.0f + (113.0f - 204.0f) * t));
        blue = static_cast<int>(std::round(21.0f + (113.0f - 21.0f) * t));
    }

    char color[16];
    std::snprintf(color, sizeof(color), "#%02x%02x%02x", red, green, blue);

    char markup[512];
    if (fraction <= 0.001f) {
        std::snprintf(markup, sizeof(markup),
            "<svg class='kill-ring-svg' viewBox='0 0 80 80'>"
            "<circle cx='40' cy='40' r='32' fill='none' stroke='#ffffff' stroke-opacity='0.10' stroke-width='6'/>"
            "</svg>");
    } else if (fraction >= 0.999f) {
        std::snprintf(markup, sizeof(markup),
            "<svg class='kill-ring-svg' viewBox='0 0 80 80'>"
            "<circle cx='40' cy='40' r='32' fill='none' stroke='#ffffff' stroke-opacity='0.10' stroke-width='6'/>"
            "<circle cx='40' cy='40' r='32' fill='none' stroke='%s' stroke-width='6'/>"
            "</svg>",
            color);
    } else {
        const float angle = fraction * 2.0f * pi;
        const float x = 40.0f + std::sin(angle) * 32.0f;
        const float y = 40.0f - std::cos(angle) * 32.0f;
        const int large_arc = fraction > 0.5f ? 1 : 0;
        std::snprintf(markup, sizeof(markup),
            "<svg class='kill-ring-svg' viewBox='0 0 80 80'>"
            "<circle cx='40' cy='40' r='32' fill='none' stroke='#ffffff' stroke-opacity='0.10' stroke-width='6'/>"
            "<path d='M 40 8 A 32 32 0 %d 1 %.2f %.2f' fill='none' stroke='%s' stroke-width='6'/>"
            "</svg>",
            large_arc, x, y, color);
    }

    return markup;
}

const char *GetWeaponIconMarkup(const char *weapon)
{
    const std::string normalized = NormalizeWeaponName(weapon);

    if (normalized == "blaster") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M8 36h25l5-7h12c4 0 7 3 7 7s-3 7-7 7H36l-5 8H17l4-8H8z'/><path d='M39 24h11l5 5H36z'/><path d='M18 43h12l-4 13H14z'/><circle cx='50' cy='36' r='3' fill='currentColor'/></svg>";
    }
    if (normalized == "shotgun") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M5 34h41v5H5z'/><path d='M43 31h15v11H43z'/><path d='M13 39h21l-7 12H16z'/><path d='M50 28h8v4h-8z'/></svg>";
    }
    if (normalized == "supershotgun") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M4 29h41v5H4z'/><path d='M4 37h41v5H4z'/><path d='M43 27h16v17H43z'/><path d='M16 42h20l-8 12H16z'/></svg>";
    }
    if (normalized == "machinegun") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M6 31h38v8H6z'/><path d='M40 27h15v16H40z'/><path d='M15 39h17l-6 14H14z'/><path d='M50 32h10v6H50z'/><path d='M23 24h18v5H23z'/></svg>";
    }
    if (normalized == "chaingun") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M7 30h31v10H7z'/><path d='M36 26h18v18H36z'/><path d='M50 29h10v3H50zM50 35h10v3H50zM50 41h10v3H50z'/><path d='M15 40h18l-7 13H14z'/><circle cx='42' cy='35' r='5' fill='currentColor'/></svg>";
    }
    if (normalized == "grenadelauncher") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M9 32h26l5-6h13v19H38l-5-6H9z'/><path d='M18 39h16l-6 13H16z'/><circle cx='48' cy='35' r='7' fill='none' stroke='currentColor' stroke-width='5'/><path d='M4 36h8v7H4z'/></svg>";
    }
    if (normalized == "rocketlauncher") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M5 28h37l10 7-10 7H5z'/><path d='M42 28l16 7-16 7z'/><path d='M17 42h19l-7 12H16z'/><path d='M8 23h25v5H8z'/></svg>";
    }
    if (normalized == "hyperblaster") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M8 31h27l6-7h14v21H40l-6-7H8z'/><path d='M18 38h17l-6 14H17z'/><circle cx='47' cy='35' r='7' fill='none' stroke='currentColor' stroke-width='4'/><circle cx='47' cy='35' r='2' fill='currentColor'/><path d='M53 30l7-4-3 8 3 8-7-4z'/></svg>";
    }
    if (normalized == "railgun") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M5 32h46v8H5z'/><path d='M48 29h11v14H48z'/><path d='M12 27h39v3H12z'/><path d='M14 40h20l-7 13H15z'/><circle cx='42' cy='36' r='3' fill='currentColor'/></svg>";
    }
    if (normalized == "bfg10k" || normalized == "bfg") {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M7 29h28l7-8h14v27H42l-7-8H7z'/><path d='M17 40h19l-7 14H16z'/><circle cx='48' cy='35' r='9' fill='none' stroke='currentColor' stroke-width='5'/><circle cx='48' cy='35' r='3' fill='currentColor'/><path d='M3 34h8v8H3z'/></svg>";
    }

    if (!normalized.empty()) {
        return "<svg class='weapon-icon-svg' viewBox='0 0 64 64'><path d='M7 33h36v7H7z'/><path d='M40 29h16v15H40z'/><path d='M16 40h18l-7 13H15z'/><path d='M10 27h20v4H10z'/></svg>";
    }

    return "";
}

} // namespace

bool CompetitiveHUD::Init(Rml::Context *ctx)
{
    if (!ctx) {
        return false;
    }

    Shutdown();

    m_ctx = ctx;
    m_doc = ctx->LoadDocument("ui/presets/competitive/competitive.rml");
    if (!m_doc) {
        m_ctx = nullptr;
        return false;
    }

    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);

    m_bottom = m_doc->GetElementById("hud-bottom");
    m_center_indicator = m_doc->GetElementById("center-indicator");
    m_hp_num = m_doc->GetElementById("hp-num");
    m_hp_value = m_doc->GetElementById("hp-value");
    m_armor_value = m_doc->GetElementById("armor-value");
    m_hp_bar = m_doc->GetElementById("hp-bar");
    m_armor_bar = m_doc->GetElementById("armor-bar");
    m_kill_num = m_doc->GetElementById("kill-num");
    m_kill_ring_art = m_doc->GetElementById("kill-ring-art");
    m_ammo_num = m_doc->GetElementById("ammo-num");
    m_ammo_bar = m_doc->GetElementById("ammo-bar");
    m_rsv_num = m_doc->GetElementById("ammo-reserve");
    m_rsv_bar = m_doc->GetElementById("reserve-bar");
    m_wpn_icon = m_doc->GetElementById("weapon-icon");
    m_wpn_name = m_doc->GetElementById("weapon-name");

    m_last_hidden = true;
    m_requested_scale = 1.0f;
    m_effective_scale = 1.0f;
    m_last_scale = -1.0f;
    m_last_opacity = -1.0f;
    m_last_viewport_width = 0;
    m_last_viewport_height = 0;
    m_last_layout_left = 0.0f;
    m_last_layout_top = 0.0f;
    m_last_layout_width = 0.0f;
    m_last_layout_height = 0.0f;
    m_last_layout_center_x = 0.0f;
    m_last_layout_center_y = 0.0f;
    m_last_hp = -1;
    m_last_armor = -1;
    m_last_kills = -1;
    m_last_ammo = -1;
    m_last_ammo_max = -1;
    m_last_reserve = -1;
    m_last_weapon[0] = 0;
    m_last_center[0] = 0;

    if (!m_hp_num || !m_hp_value || !m_armor_value || !m_hp_bar || !m_armor_bar || !m_kill_num ||
        !m_kill_ring_art || !m_ammo_num || !m_ammo_bar ||
        !m_rsv_num || !m_rsv_bar || !m_wpn_icon || !m_wpn_name ||
        !m_bottom || !m_center_indicator) {
        Shutdown();
        return false;
    }

    UpdateVisibility(true);
    return true;
}

void CompetitiveHUD::SetScale(float scale)
{
    const float requested_scale = clamp_float(scale, 0.5f, 6.0f);
    scale = requested_scale;
    int viewport_width = 0;
    int viewport_height = 0;
    if (m_ctx) {
        const Rml::Vector2i dimensions = m_ctx->GetDimensions();
        viewport_width = std::max(1, dimensions.x);
        viewport_height = std::max(1, dimensions.y);
    }
    const bool viewport_changed = viewport_width != m_last_viewport_width || viewport_height != m_last_viewport_height;
    if (!m_bottom || (std::fabs(scale - m_last_scale) < 0.001f &&
                      std::fabs(requested_scale - m_requested_scale) < 0.001f &&
                      !viewport_changed)) {
        return;
    }
    m_requested_scale = requested_scale;
    m_effective_scale = scale;
    m_last_scale = scale;
    m_last_viewport_width = viewport_width;
    m_last_viewport_height = viewport_height;

    // The current stretch-pic backend does not reliably preserve CSS transforms,
    // so the competitive HUD uses explicit center-anchored geometry.
    if (m_bottom) {
        m_bottom->SetProperty("transform", "none");
    }

    SetPx(m_bottom, "left", 0.0f);
    SetPx(m_bottom, "top", 0.0f);
    SetPx(m_bottom, "width", static_cast<float>(std::max(1, viewport_width)));
    SetPx(m_bottom, "height", static_cast<float>(std::max(1, viewport_height)));
    SetPx(m_bottom, "padding-bottom", 0.0f);

    SetScaledFont(m_center_indicator, 22.0f, scale);
    SetPx(m_center_indicator, "line-height", 26.0f * scale);

    if (Rml::Element *hp_block = m_doc->GetElementById("hp-block")) SetPx(hp_block, "width", HUD_BASE_HP_WIDTH * scale);
    if (Rml::Element *hp_bars = m_doc->GetElementById("hp-bars")) {
        SetPx(hp_bars, "width", 92.0f * scale);
        SetPx(hp_bars, "margin-left", 14.0f * scale);
        SetPx(hp_bars, "gap", 7.0f * scale);
    }
    SetScaledFont(m_hp_num, 50.0f, scale);
    SetScaledFont(m_hp_value, 11.0f, scale);
    SetScaledFont(m_armor_value, 11.0f, scale);

    Rml::ElementList stripes;
    m_doc->QuerySelectorAll(stripes, ".stripe");
    for (Rml::Element *stripe : stripes) {
        SetScaledPx(stripe, "width", HUD_BASE_STRIPE_WIDTH, scale);
        SetPx(stripe, "height", HUD_BASE_STRIPE_HEIGHT * scale);
        SetPx(stripe, "margin-left", 0.0f);
        SetPx(stripe, "margin-right", 0.0f);
    }

    if (Rml::Element *kill_block = m_doc->GetElementById("kill-block")) SetPx(kill_block, "gap", 7.0f * scale);
    if (Rml::Element *kill_ring = m_doc->GetElementById("kill-ring")) {
        SetPx(kill_ring, "width", HUD_BASE_KILL_WIDTH * scale);
        SetPx(kill_ring, "height", HUD_BASE_KILL_WIDTH * scale);
    }
    SetPx(m_kill_ring_art, "width", HUD_BASE_KILL_WIDTH * scale);
    SetPx(m_kill_ring_art, "height", HUD_BASE_KILL_WIDTH * scale);
    SetScaledFont(m_kill_num, 21.0f, scale);
    SetPx(m_kill_num, "line-height", HUD_BASE_KILL_WIDTH * scale);

    if (Rml::Element *ammo_block = m_doc->GetElementById("ammo-block")) SetPx(ammo_block, "width", HUD_BASE_AMMO_WIDTH * scale);
    if (Rml::Element *ammo_bars = m_doc->GetElementById("ammo-bars")) {
        SetPx(ammo_bars, "width", 92.0f * scale);
        SetPx(ammo_bars, "margin-right", 14.0f * scale);
        SetPx(ammo_bars, "gap", 7.0f * scale);
    }
    if (Rml::Element *ammo_text_row = m_doc->GetElementById("ammo-text-row")) SetPx(ammo_text_row, "gap", 14.0f * scale);
    if (Rml::Element *ammo_count_col = m_doc->GetElementById("ammo-count-col")) SetPx(ammo_count_col, "min-width", 76.0f * scale);
    SetScaledFont(m_ammo_num, 50.0f, scale);
    SetScaledFont(m_rsv_num, 18.0f, scale);
    SetPx(m_rsv_num, "margin-top", 4.0f * scale);

    if (Rml::Element *weapon_meta = m_doc->GetElementById("weapon-meta")) {
        SetPx(weapon_meta, "gap", 10.0f * scale);
        SetPx(weapon_meta, "min-width", 190.0f * scale);
    }
    SetPx(m_wpn_icon, "width", 54.0f * scale);
    SetPx(m_wpn_icon, "height", 54.0f * scale);
    SetPx(m_wpn_name, "max-width", 240.0f * scale);
    SetScaledFont(m_wpn_name, 22.0f, scale);
    SetPx(m_wpn_name, "min-height", 28.0f * scale);

    Rml::ElementList labels;
    m_doc->QuerySelectorAll(labels, ".side-label");
    for (Rml::Element *label : labels) {
        SetScaledFont(label, 11.0f, scale);
    }
    labels.clear();
    m_doc->QuerySelectorAll(labels, ".bar-label");
    for (Rml::Element *label : labels) {
        SetScaledFont(label, 11.0f, scale);
    }
    labels.clear();
    m_doc->QuerySelectorAll(labels, ".bar-value");
    for (Rml::Element *label : labels) {
        SetScaledFont(label, 11.0f, scale);
    }
    labels.clear();
    m_doc->QuerySelectorAll(labels, ".bar-track");
    for (Rml::Element *bar : labels) {
        SetScaledPx(bar, "height", 4.0f, scale);
    }
    labels.clear();
    m_doc->QuerySelectorAll(labels, ".bar-fill");
    for (Rml::Element *bar : labels) {
        SetScaledPx(bar, "height", 4.0f, scale);
    }

    Layout(scale, viewport_width, viewport_height);
}

void CompetitiveHUD::Layout(float scale, int viewport_width, int viewport_height)
{
    if (!m_doc || !m_bottom || viewport_width <= 0 || viewport_height <= 0) {
        return;
    }

    Rml::Element *hp_block = m_doc->GetElementById("hp-block");
    Rml::Element *kill_block = m_doc->GetElementById("kill-block");
    Rml::Element *ammo_block = m_doc->GetElementById("ammo-block");

    Rml::ElementList stripes;
    m_doc->QuerySelectorAll(stripes, ".stripe");

    const float viewport_w = static_cast<float>(viewport_width);
    const float viewport_h = static_cast<float>(viewport_height);
    const float center_x = viewport_w * 0.5f;

    const float bottom_pad = HUD_BASE_BOTTOM_PAD * scale;
    const float content_bottom = viewport_h - bottom_pad;
    const float side_slot_w = HUD_BASE_SIDE_SLOT_WIDTH * scale;
    const float hp_w = HUD_BASE_HP_WIDTH * scale;
    const float ammo_w = HUD_BASE_AMMO_WIDTH * scale;
    const float stripe_w = std::max(1.0f, HUD_BASE_STRIPE_WIDTH * scale);
    const float stripe_gap = HUD_BASE_STRIPE_GAP * scale;
    const float stripe_h = HUD_BASE_STRIPE_HEIGHT * scale;
    const float kill_w = HUD_BASE_KILL_WIDTH * scale;
    const float kill_h = HUD_BASE_KILL_BLOCK_HEIGHT * scale;
    const float side_h = HUD_BASE_SIDE_BLOCK_HEIGHT * scale;

    const float kill_left = center_x - kill_w * 0.5f;
    const float kill_top = content_bottom - kill_h;
    const float left_stripe_left = kill_left - stripe_gap - stripe_w;
    const float right_stripe_left = kill_left + kill_w + stripe_gap;
    const float left_slot_left = left_stripe_left - stripe_gap - side_slot_w;
    const float right_slot_left = right_stripe_left + stripe_w + stripe_gap;
    const float hp_left = left_slot_left + (side_slot_w - hp_w);
    const float ammo_left = right_slot_left;
    const float side_top = content_bottom - side_h;
    const float stripe_top = content_bottom - stripe_h;

    SetRect(hp_block, hp_left, side_top, hp_w, side_h);
    SetRect(kill_block, kill_left, kill_top, kill_w, kill_h);
    SetRect(ammo_block, ammo_left, side_top, ammo_w, side_h);

    if (stripes.size() > 0) {
        SetRect(stripes[0], left_stripe_left, stripe_top, stripe_w, stripe_h);
    }
    if (stripes.size() > 1) {
        SetRect(stripes[1], right_stripe_left, stripe_top, stripe_w, stripe_h);
    }

    m_last_layout_left = center_x - (CompetitiveBaseWidth() * scale) * 0.5f;
    m_last_layout_top = viewport_h - (HUD_BASE_HEIGHT * scale);
    m_last_layout_width = CompetitiveBaseWidth() * scale;
    m_last_layout_height = HUD_BASE_HEIGHT * scale;
    m_last_layout_center_x = center_x;
    m_last_layout_center_y = m_last_layout_top + m_last_layout_height * 0.5f;
}

void CompetitiveHUD::SetOpacity(float alpha)
{
    alpha = clamp_float(alpha, 0.0f, 1.0f);
    if (!m_bottom || std::fabs(alpha - m_last_opacity) < 0.001f) {
        return;
    }
    m_last_opacity = alpha;

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.3f", alpha);
    m_bottom->SetProperty("opacity", buf);
}

void CompetitiveHUD::Update(const CompetitiveHUDState& state)
{
    if (!m_doc) {
        return;
    }

    if (state.hidden != m_last_hidden) {
        UpdateVisibility(state.hidden);
        m_last_hidden = state.hidden;
    }
    if (state.hidden) {
        return;
    }

    const int hp = clamp_int(state.health, 0, 999);
    const int armor = clamp_int(state.armor, 0, 999);
    const int kills = clamp_int(state.kills, 0, 999);
    const int ammo = clamp_int(state.ammo, 0, 999);
    const int ammo_max = clamp_int(state.ammo_max, 0, 999);
    const int reserve = clamp_int(state.ammo_reserve, 0, 999);

    if (hp != m_last_hp || armor != m_last_armor) {
        UpdateHP(hp, armor);
        m_last_hp = hp;
        m_last_armor = armor;
    }
    if (kills != m_last_kills) {
        UpdateKillRing(kills);
        m_last_kills = kills;
    }
    if (ammo != m_last_ammo || ammo_max != m_last_ammo_max ||
        reserve != m_last_reserve || std::strncmp(state.weapon_name, m_last_weapon, sizeof(m_last_weapon)) != 0) {
        UpdateAmmo(ammo, ammo_max, reserve, state.weapon_name);
        m_last_ammo = ammo;
        m_last_ammo_max = ammo_max;
        m_last_reserve = reserve;
        std::snprintf(m_last_weapon, sizeof(m_last_weapon), "%s", state.weapon_name);
    }
    UpdateCenterIndicator(state.pickup, state.pickup_alpha);
}

void CompetitiveHUD::UpdateVisibility(bool hidden)
{
    if (m_doc) {
        m_doc->SetClass("hidden", hidden);
    }
}

void CompetitiveHUD::DebugDescribe(bool enabled, const char *reason)
{
    if (!enabled) {
        return;
    }

    Com_LPrintf(PRINT_DEVELOPER,
        "RmlUi competitive HUD: %s doc=%p ctx=%p hidden=%d viewport=%dx%d requested_scale=%.3f effective_scale=%.3f base_width=%.1f scaled_width=%.1f layout_left=%.1f layout_top=%.1f layout_size=%.1fx%.1f center=(%.1f,%.1f) health=%d armor=%d combined_health=%d\n",
        reason ? reason : "diagnostic",
        static_cast<void *>(m_doc),
        static_cast<void *>(m_ctx),
        m_last_hidden ? 1 : 0,
        m_last_viewport_width,
        m_last_viewport_height,
        m_requested_scale,
        m_effective_scale,
        CompetitiveBaseWidth(),
        m_last_layout_width,
        m_last_layout_left,
        m_last_layout_top,
        m_last_layout_width,
        m_last_layout_height,
        m_last_layout_center_x,
        m_last_layout_center_y,
        m_last_hp,
        m_last_armor,
        clamp_int(m_last_hp, 0, 999) + clamp_int(m_last_armor, 0, 999));
}

void CompetitiveHUD::UpdateHP(int hp, int armor)
{
    char buffer[16];
    const int combined_health = clamp_int(hp + armor, 0, 999);

    std::snprintf(buffer, sizeof(buffer), "%d", combined_health);
    SetInnerText(m_hp_num, buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", hp);
    SetInnerText(m_hp_value, buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", armor);
    SetInnerText(m_armor_value, buffer);

    SetWidth(m_hp_bar, static_cast<float>(clamp_int(hp, 0, 100)));
    SetWidth(m_armor_bar, static_cast<float>(clamp_int(armor, 0, 100)));

    m_hp_num->SetClass("warn", combined_health <= 60 && combined_health > 30);
    m_hp_num->SetClass("crit", combined_health <= 30);
    m_hp_bar->SetClass("warn", hp <= 60 && hp > 30);
    m_hp_bar->SetClass("crit", hp <= 30);
}

void CompetitiveHUD::UpdateKillRing(int kills)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", kills);
    SetInnerText(m_kill_num, buffer);
    SetInnerText(m_kill_ring_art, GetKillRingMarkup(kills).c_str());
}

void CompetitiveHUD::UpdateCenterIndicator(const char *text, float alpha)
{
    if (!m_center_indicator) {
        return;
    }

    alpha = clamp_float(alpha, 0.0f, 1.0f);
    char opacity[16];
    std::snprintf(opacity, sizeof(opacity), "%.3f", alpha);
    m_center_indicator->SetProperty("opacity", opacity);

    if (alpha <= 0.001f || !text || !text[0]) {
        if (m_last_center[0]) {
            SetInnerText(m_center_indicator, "");
            m_last_center[0] = 0;
        }
        return;
    }

    if (std::strncmp(text, m_last_center, sizeof(m_last_center)) != 0) {
        const std::string escaped = EscapeRmlText(text);
        SetInnerText(m_center_indicator, escaped.c_str());
        std::snprintf(m_last_center, sizeof(m_last_center), "%s", text);
    }
}

void CompetitiveHUD::UpdateAmmo(int ammo, int ammo_max, int reserve, const char *weapon)
{
    char buffer[32];

    std::snprintf(buffer, sizeof(buffer), "%d", ammo);
    SetInnerText(m_ammo_num, buffer);

    std::snprintf(buffer, sizeof(buffer), "/ %d", reserve);
    SetInnerText(m_rsv_num, buffer);

    const std::string weapon_text = EscapeRmlText(weapon && weapon[0] ? weapon : "-");
    SetInnerText(m_wpn_name, weapon_text.c_str());
    SetInnerText(m_wpn_icon, GetWeaponIconMarkup(weapon));

    const float ammo_percent = ammo_max > 0 ? (static_cast<float>(ammo) / static_cast<float>(ammo_max)) * 100.0f : 0.0f;
    const float reserve_percent = ammo_max > 0 ? (static_cast<float>(reserve) / (static_cast<float>(ammo_max) * 3.0f)) * 100.0f : 0.0f;

    SetWidth(m_ammo_bar, ammo_percent);
    SetWidth(m_rsv_bar, reserve_percent);
    m_ammo_bar->SetClass("low", ammo_percent < 33.0f);
}

void CompetitiveHUD::Shutdown()
{
    if (m_doc) {
        m_doc->Close();
    }

    m_ctx = nullptr;
    m_doc = nullptr;
    m_center_indicator = nullptr;
    m_hp_num = nullptr;
    m_hp_value = nullptr;
    m_armor_value = nullptr;
    m_hp_bar = nullptr;
    m_armor_bar = nullptr;
    m_kill_num = nullptr;
    m_kill_ring_art = nullptr;
    m_ammo_num = nullptr;
    m_ammo_bar = nullptr;
    m_rsv_num = nullptr;
    m_rsv_bar = nullptr;
    m_wpn_icon = nullptr;
    m_wpn_name = nullptr;
    m_bottom = nullptr;
    m_requested_scale = 1.0f;
    m_effective_scale = 1.0f;
    m_last_viewport_width = 0;
    m_last_viewport_height = 0;
    m_last_layout_left = 0.0f;
    m_last_layout_top = 0.0f;
    m_last_layout_width = 0.0f;
    m_last_layout_height = 0.0f;
    m_last_layout_center_x = 0.0f;
    m_last_layout_center_y = 0.0f;
    m_last_center[0] = 0;
}
