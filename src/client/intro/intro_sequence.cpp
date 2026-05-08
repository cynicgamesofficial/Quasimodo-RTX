#include "intro_sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

Rml::String px(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1fpx", value);
    return buffer;
}

Rml::String pct(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", value);
    return buffer;
}

void set_display(Rml::Element *element, bool visible)
{
    if (element) {
        element->SetProperty("display", visible ? "block" : "none");
    }
}

Rml::Element *append_div(Rml::Element *parent, const char *class_name)
{
    if (!parent) {
        return nullptr;
    }
    Rml::ElementPtr element = parent->GetOwnerDocument()->CreateElement("div");
    if (!element) {
        return nullptr;
    }
    if (class_name && class_name[0]) {
        element->SetAttribute("class", class_name);
    }
    Rml::Element *raw = element.get();
    parent->AppendChild(std::move(element));
    return raw;
}

} // namespace

float IntroSequence::Clamp01(float t)
{
    return std::max(0.0f, std::min(t, 1.0f));
}

float IntroSequence::EaseOut(float t)
{
    t = Clamp01(t);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float IntroSequence::Random01()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

bool IntroSequence::Init(Rml::Context *ctx, DoneCallback on_done)
{
    if (!ctx) {
        return false;
    }

    Shutdown();
    m_ctx = ctx;
    m_on_done = std::move(on_done);
    m_doc = ctx->LoadDocument("ui/intro/intro.rml");
    if (!m_doc) {
        m_ctx = nullptr;
        return false;
    }
    m_doc->Show();

    m_stage_cynic = m_doc->GetElementById("stage-cynic");
    m_stage_engine = m_doc->GetElementById("stage-engine");
    m_stage_done = m_doc->GetElementById("stage-done");
    m_bg_layer = m_doc->GetElementById("bg-layer");
    m_scanlines = m_doc->GetElementById("scanlines");
    m_cynic_text = m_doc->GetElementById("cynic-text");
    m_cynic_label = m_doc->GetElementById("cynic-label");
    m_cynic_sub = m_doc->GetElementById("cynic-sub");
    m_engine_transversal_line = m_doc->GetElementById("engine-transversal-line");
    m_engine_logo_wrap = m_doc->GetElementById("engine-logo-wrap");
    m_engine_logo = m_doc->GetElementById("engine-logo");
    m_engine_logo_echo_a = m_doc->GetElementById("engine-logo-echo-a");
    m_engine_logo_echo_b = m_doc->GetElementById("engine-logo-echo-b");
    m_engine_logo_halo = m_doc->GetElementById("engine-logo-halo");
    m_engine_logo_sheen = m_doc->GetElementById("engine-logo-sheen");
    m_engine_title = m_doc->GetElementById("engine-title");
    m_engine_mask = m_doc->GetElementById("engine-mask");
    m_engine_sub = m_doc->GetElementById("engine-sub");
    m_engine_ver = m_doc->GetElementById("engine-ver");

    if (!m_stage_cynic || !m_stage_engine || !m_stage_done || !m_bg_layer ||
        !m_cynic_text || !m_cynic_label || !m_cynic_sub ||
        !m_engine_transversal_line || !m_engine_logo_wrap || !m_engine_logo ||
        !m_engine_logo_echo_a || !m_engine_logo_echo_b || !m_engine_logo_halo ||
        !m_engine_logo_sheen || !m_engine_title || !m_engine_mask ||
        !m_engine_sub || !m_engine_ver) {
        Shutdown();
        return false;
    }

    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        m_particles[i].element = append_div(m_bg_layer, "particle");
    }
    if (m_scanlines) {
        for (int i = 0; i < SCANLINE_COUNT; ++i) {
            Rml::Element *line = append_div(m_scanlines, "scanline");
            if (line) {
                line->SetProperty("top", pct((static_cast<float>(i) / static_cast<float>(SCANLINE_COUNT)) * 100.0f));
            }
        }
    }

    const Rml::Vector2i dimensions = ctx->GetDimensions();
    m_screen_w = dimensions.x;
    m_screen_h = dimensions.y;
    InitParticles();
    ResetVisualState();

    m_phase = 0;
    m_phase_t = 0.0f;
    m_total_t = 0.0f;
    m_playing = true;
    m_done_called = false;
    m_done_pending = false;
    return true;
}

void IntroSequence::ResetVisualState()
{
    SetOpacity(m_stage_cynic, 0.0f);
    SetOpacity(m_stage_engine, 0.0f);
    SetOpacity(m_stage_done, 0.0f);
    SetHeight(m_cynic_text, 0.0f);
    SetOpacity(m_cynic_label, 0.0f);
    SetTranslateY(m_cynic_label, 8.0f);
    SetColor(m_cynic_sub, 1.0f, 1.0f, 1.0f, 0.0f);
    SetOpacity(m_engine_transversal_line, 0.0f);
    SetRect(m_engine_transversal_line, 60.0f, 224.0f, 0.0f, 5.0f);
    SetWidth(m_engine_mask, 100.0f);
    SetOpacity(m_engine_title, 0.0f);
    SetTranslateY(m_engine_title, 12.0f);
    SetColor(m_engine_sub, 1.0f, 1.0f, 1.0f, 0.0f);
    SetColor(m_engine_ver, 1.0f, 1.0f, 1.0f, 0.0f);
    DrawEngineLogo(0.0f);
}

void IntroSequence::InitParticles()
{
    for (Particle& particle : m_particles) {
        particle.x = Random01();
        particle.y = 0.5f + Random01();
        particle.vx = (Random01() - 0.5f) * 0.6f;
        particle.vy = -(Random01() * 0.4f + 0.1f);
        particle.radius = Random01() * 1.2f + 0.3f;
        particle.alpha = Random01() * 0.4f + 0.05f;
    }
}

void IntroSequence::TickParticles(float dt)
{
    for (Particle& particle : m_particles) {
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
        if (particle.y < -0.05f) {
            particle.y = 1.1f;
            particle.x = Random01();
        }
        if (particle.x < 0.0f || particle.x > 1.0f) {
            particle.vx = -particle.vx;
        }
    }
}

void IntroSequence::DrawParticles()
{
    if (!m_ctx) {
        return;
    }

    const Rml::Vector2i dimensions = m_ctx->GetDimensions();
    m_screen_w = std::max(1, dimensions.x);
    m_screen_h = std::max(1, dimensions.y);

    for (Particle& particle : m_particles) {
        if (!particle.element) {
            continue;
        }
        const float size = particle.radius * 2.0f;
        SetRect(particle.element,
                particle.x * static_cast<float>(m_screen_w) - particle.radius,
                particle.y * static_cast<float>(m_screen_h) - particle.radius,
                size, size);
        char color[48];
        std::snprintf(color, sizeof(color), "rgba(180,200,255,%.3f)", particle.alpha);
        particle.element->SetProperty("background-color", color);
        particle.element->SetProperty("border-radius", px(particle.radius));
    }
}

void IntroSequence::Update(float dt_sec)
{
    if (!m_playing) {
        return;
    }

    dt_sec = std::max(0.0f, std::min(dt_sec, 0.25f));
    m_phase_t += dt_sec;
    m_total_t += dt_sec;

    TickParticles(dt_sec);
    DrawParticles();

    switch (m_phase) {
    case 0: {
        const float p = EaseOut(Clamp01(m_phase_t / CYNIC_DUR));
        SetOpacity(m_stage_cynic, Clamp01(m_phase_t / 0.3f));
        if (p > 0.35f) {
            const float q = Clamp01((p - 0.35f) / 0.25f);
            SetHeight(m_cynic_text, 40.0f);
            SetOpacity(m_cynic_label, q);
            SetTranslateY(m_cynic_label, 8.0f * (1.0f - q));
        }
        if (p > 0.65f) {
            const float q = Clamp01((p - 0.65f) / 0.25f);
            SetColor(m_cynic_sub, 1.0f, 1.0f, 1.0f, q * 0.45f);
        }
        if (m_phase_t >= CYNIC_DUR + CYNIC_HOLD) {
            AdvancePhase();
        }
        break;
    }
    case 1: {
        const float fade = Clamp01(m_phase_t / FADE_DUR);
        SetOpacity(m_stage_cynic, 1.0f - fade);
        if (fade >= 1.0f) {
            AdvancePhase();
        }
        break;
    }
    case 2: {
        SetOpacity(m_stage_cynic, 0.0f);
        const float p = EaseOut(Clamp01(m_phase_t / ENGINE_DUR));
        SetOpacity(m_stage_engine, Clamp01(m_phase_t / 0.4f));
        DrawEngineLogo(p);
        if (p > 0.4f) {
            const float q = EaseOut(Clamp01((p - 0.4f) / 0.34f));
            SetOpacity(m_engine_title, q);
            SetTranslateY(m_engine_title, 12.0f * (1.0f - q));
            SetWidth(m_engine_mask, (1.0f - q) * 100.0f);
        }
        if (p > 0.75f) {
            SetColor(m_engine_sub, 1.0f, 1.0f, 1.0f, 0.45f);
        }
        if (p > 0.88f) {
            SetColor(m_engine_ver, 1.0f, 1.0f, 1.0f, 0.22f);
        }
        if (m_phase_t >= ENGINE_DUR + ENGINE_HOLD) {
            AdvancePhase();
        }
        break;
    }
    case 3: {
        const float fade = Clamp01(m_phase_t / FADE_DUR);
        SetOpacity(m_stage_engine, 1.0f - fade);
        SetOpacity(m_stage_done, fade);
        if (fade >= 1.0f) {
            AdvancePhase();
        }
        break;
    }
    case 4: {
        const float pulse = 0.5f + 0.5f * std::sin(m_phase_t * 2.0f);
        SetOpacity(m_stage_done, pulse);
        break;
    }
    default:
        break;
    }
}

void IntroSequence::AdvancePhase()
{
    ++m_phase;
    m_phase_t = 0.0f;
    if (m_phase > 4) {
        m_phase = 4;
    }
}

void IntroSequence::DrawEngineLogo(float progress)
{
    const float p = Clamp01(progress);
    const float reveal = EaseOut(Clamp01(p / 0.56f));
    const float settle = EaseOut(Clamp01((p - 0.46f) / 0.42f));
    const float logo_scale = 0.70f + 0.32f * reveal - 0.02f * settle;
    const float logo_tilt = -8.0f * (1.0f - reveal);
    const float echo_a = Clamp01((p - 0.06f) / 0.16f) * (1.0f - Clamp01((p - 0.42f) / 0.22f));
    const float echo_b = Clamp01((p - 0.16f) / 0.16f) * (1.0f - Clamp01((p - 0.58f) / 0.24f));

    SetOpacity(m_engine_logo_wrap, reveal);
    SetOpacity(m_engine_logo, reveal);
    SetScaleRotate(m_engine_logo, logo_scale, logo_tilt);

    SetOpacity(m_engine_logo_echo_a, echo_a * 0.34f);
    SetScaleRotate(m_engine_logo_echo_a, 1.10f + 0.22f * echo_a, -5.0f + 5.0f * reveal);
    SetOpacity(m_engine_logo_echo_b, echo_b * 0.22f);
    SetScaleRotate(m_engine_logo_echo_b, 0.92f + 0.38f * echo_b, 5.0f - 5.0f * reveal);

    SetOpacity(m_engine_logo_halo, 0.05f + reveal * 0.45f);
    SetScaleRotate(m_engine_logo_halo, 0.62f + 0.42f * reveal, 0.0f);

    const float line_in = EaseOut(Clamp01((p - 0.12f) / 0.30f));
    const float line_out = 1.0f - Clamp01((p - 0.94f) / 0.06f);
    SetOpacity(m_engine_transversal_line, line_in * line_out);
    SetRect(m_engine_transversal_line, 60.0f, 224.0f, 660.0f * line_in, 5.0f);

    const float sheen = Clamp01((p - 0.26f) / 0.34f);
    const float sheen_fade = 1.0f - Clamp01((p - 0.74f) / 0.20f);
    SetOpacity(m_engine_logo_sheen, sheen * sheen_fade * 0.40f);
    SetRect(m_engine_logo_sheen, -42.0f + 350.0f * sheen, 28.0f, 26.0f, 216.0f);
}

void IntroSequence::Skip()
{
    if (!m_playing) {
        return;
    }

    m_playing = false;
    if (m_doc) {
        m_doc->Close();
        m_doc = nullptr;
    }
    m_done_pending = true;
}

void IntroSequence::FlushDoneCallback()
{
    if (!m_done_pending || m_done_called || !m_on_done) {
        return;
    }

    m_done_pending = false;
    m_done_called = true;
    m_on_done();
}

void IntroSequence::Shutdown()
{
    if (m_doc) {
        m_doc->Close();
    }
    m_ctx = nullptr;
    m_doc = nullptr;
    m_on_done = nullptr;
    m_playing = false;
    m_done_called = false;
    m_done_pending = false;
    m_stage_cynic = nullptr;
    m_stage_engine = nullptr;
    m_stage_done = nullptr;
    m_bg_layer = nullptr;
    m_scanlines = nullptr;
    m_cynic_text = nullptr;
    m_cynic_label = nullptr;
    m_cynic_sub = nullptr;
    m_engine_transversal_line = nullptr;
    m_engine_logo_wrap = nullptr;
    m_engine_logo = nullptr;
    m_engine_logo_echo_a = nullptr;
    m_engine_logo_echo_b = nullptr;
    m_engine_logo_halo = nullptr;
    m_engine_logo_sheen = nullptr;
    m_engine_title = nullptr;
    m_engine_mask = nullptr;
    m_engine_sub = nullptr;
    m_engine_ver = nullptr;
    for (Particle& particle : m_particles) particle.element = nullptr;
}

void IntroSequence::SetOpacity(Rml::Element *element, float value)
{
    if (!element) {
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", Clamp01(value));
    element->SetProperty("opacity", buffer);
}

void IntroSequence::SetColor(Rml::Element *element, float r, float g, float b, float a)
{
    if (!element) {
        return;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "rgba(%d,%d,%d,%.3f)",
        static_cast<int>(Clamp01(r) * 255.0f),
        static_cast<int>(Clamp01(g) * 255.0f),
        static_cast<int>(Clamp01(b) * 255.0f),
        Clamp01(a));
    element->SetProperty("color", buffer);
}

void IntroSequence::SetWidth(Rml::Element *element, float value)
{
    if (element) {
        element->SetProperty("width", pct(value));
    }
}

void IntroSequence::SetHeight(Rml::Element *element, float value)
{
    if (element) {
        element->SetProperty("height", px(value));
    }
}

void IntroSequence::SetTranslateY(Rml::Element *element, float value)
{
    if (!element) {
        return;
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "translateY(%.1fpx)", value);
    element->SetProperty("transform", buffer);
}

void IntroSequence::SetScaleRotate(Rml::Element *element, float scale, float degrees)
{
    if (!element) {
        return;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "scale(%.3f) rotate(%.1fdeg)", scale, degrees);
    element->SetProperty("transform", buffer);
}

void IntroSequence::SetRect(Rml::Element *element, float x, float y, float w, float h)
{
    if (!element) {
        return;
    }
    element->SetProperty("left", px(x));
    element->SetProperty("top", px(y));
    element->SetProperty("width", px(std::max(0.0f, w)));
    element->SetProperty("height", px(std::max(0.0f, h)));
}
