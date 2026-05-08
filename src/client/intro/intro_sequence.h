#pragma once

#include <RmlUi/Core.h>

#include <functional>

class IntroSequence {
public:
    using DoneCallback = std::function<void()>;

    bool Init(Rml::Context *ctx, DoneCallback on_done);
    void Update(float dt_sec);
    bool IsPlaying() const { return m_playing; }
    void Skip();
    void FlushDoneCallback();
    void Shutdown();

private:
    struct Particle {
        float x;
        float y;
        float vx;
        float vy;
        float radius;
        float alpha;
        Rml::Element *element;
    };

    void AdvancePhase();
    void InitParticles();
    void TickParticles(float dt);
    void DrawParticles();
    void DrawEngineLogo(float progress);
    void ResetVisualState();

    static float EaseOut(float t);
    static float Clamp01(float t);
    static float Random01();

    void SetOpacity(Rml::Element *element, float value);
    void SetColor(Rml::Element *element, float r, float g, float b, float a);
    void SetWidth(Rml::Element *element, float pct);
    void SetHeight(Rml::Element *element, float px);
    void SetTranslateY(Rml::Element *element, float px);
    void SetScaleRotate(Rml::Element *element, float scale, float degrees);
    void SetRect(Rml::Element *element, float x, float y, float w, float h);

    Rml::Context *m_ctx = nullptr;
    Rml::ElementDocument *m_doc = nullptr;
    DoneCallback m_on_done;
    bool m_playing = false;
    bool m_done_called = false;
    bool m_done_pending = false;

    int m_phase = 0;
    float m_phase_t = 0.0f;
    float m_total_t = 0.0f;
    int m_screen_w = 0;
    int m_screen_h = 0;

    static constexpr float CYNIC_DUR = 1.450f;
    static constexpr float CYNIC_HOLD = 0.350f;
    static constexpr float FADE_DUR = 0.600f;
    static constexpr float ENGINE_DUR = 4.200f;
    static constexpr float ENGINE_HOLD = 1.600f;
    static constexpr int PARTICLE_COUNT = 60;
    static constexpr int SCANLINE_COUNT = 64;

    Particle m_particles[PARTICLE_COUNT] = {};

    Rml::Element *m_stage_cynic = nullptr;
    Rml::Element *m_stage_engine = nullptr;
    Rml::Element *m_stage_done = nullptr;
    Rml::Element *m_bg_layer = nullptr;
    Rml::Element *m_scanlines = nullptr;
    Rml::Element *m_cynic_text = nullptr;
    Rml::Element *m_cynic_label = nullptr;
    Rml::Element *m_cynic_sub = nullptr;
    Rml::Element *m_engine_transversal_line = nullptr;
    Rml::Element *m_engine_logo_wrap = nullptr;
    Rml::Element *m_engine_logo = nullptr;
    Rml::Element *m_engine_logo_echo_a = nullptr;
    Rml::Element *m_engine_logo_echo_b = nullptr;
    Rml::Element *m_engine_logo_halo = nullptr;
    Rml::Element *m_engine_logo_sheen = nullptr;
    Rml::Element *m_engine_title = nullptr;
    Rml::Element *m_engine_mask = nullptr;
    Rml::Element *m_engine_sub = nullptr;
    Rml::Element *m_engine_ver = nullptr;
};
