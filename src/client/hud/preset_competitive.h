#pragma once

#include <RmlUi/Core.h>

struct CompetitiveHUDState {
    int health;
    int armor;
    int kills;
    int ammo;
    int ammo_max;
    int ammo_reserve;
    bool hidden;
    float pickup_alpha;
    char weapon_name[32];
    const char *pickup;
};

class CompetitiveHUD {
public:
    static constexpr int MAX_KILLS_RING = 15;

    bool Init(Rml::Context *ctx);
    void Update(const CompetitiveHUDState& state);
    void SetScale(float scale);
    void SetOpacity(float alpha);
    void DebugDescribe(bool enabled, const char *reason);
    void Shutdown();
    bool IsInitialized() const { return m_doc != nullptr; }

private:
    void Layout(float scale, int viewport_width, int viewport_height);
    void UpdateVisibility(bool hidden);
    void UpdateHP(int hp, int armor);
    void UpdateKillRing(int kills);
    void UpdateAmmo(int ammo, int ammo_max, int reserve, const char *weapon);
    void UpdateCenterIndicator(const char *text, float alpha);

    Rml::Context *m_ctx = nullptr;
    Rml::ElementDocument *m_doc = nullptr;
    Rml::Element *m_bottom = nullptr;
    Rml::Element *m_center_indicator = nullptr;
    Rml::Element *m_hp_num = nullptr;
    Rml::Element *m_hp_value = nullptr;
    Rml::Element *m_armor_value = nullptr;
    Rml::Element *m_hp_bar = nullptr;
    Rml::Element *m_armor_bar = nullptr;
    Rml::Element *m_kill_num = nullptr;
    Rml::Element *m_kill_ring_art = nullptr;
    Rml::Element *m_ammo_num = nullptr;
    Rml::Element *m_ammo_bar = nullptr;
    Rml::Element *m_rsv_num = nullptr;
    Rml::Element *m_rsv_bar = nullptr;
    Rml::Element *m_wpn_icon = nullptr;
    Rml::Element *m_wpn_name = nullptr;

    bool m_last_hidden = true;
    float m_requested_scale = 1.0f;
    float m_effective_scale = 1.0f;
    float m_last_scale = 1.0f;
    float m_last_opacity = 1.0f;
    int m_last_viewport_width = 0;
    int m_last_viewport_height = 0;
    float m_last_layout_left = 0.0f;
    float m_last_layout_top = 0.0f;
    float m_last_layout_width = 0.0f;
    float m_last_layout_height = 0.0f;
    float m_last_layout_center_x = 0.0f;
    float m_last_layout_center_y = 0.0f;
    int m_last_hp = -1;
    int m_last_armor = -1;
    int m_last_kills = -1;
    int m_last_ammo = -1;
    int m_last_ammo_max = -1;
    int m_last_reserve = -1;
    char m_last_weapon[32] = {};
    char m_last_center[512] = {};
};
