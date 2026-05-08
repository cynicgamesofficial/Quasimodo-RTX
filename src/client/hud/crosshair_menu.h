#pragma once

#include <RmlUi/Core.h>

#include "crosshair.h"

class CrosshairMenu : public Rml::EventListener {
public:
    bool Init(Rml::Context *ctx, CrosshairHUD *crosshair);
    void Show();
    void Hide();
    void SetTransparentBackground(bool transparent);
    bool IsVisible() const { return m_visible; }
    void Toggle();
    void Shutdown();
    bool TakeCloseRequest();

    void ProcessEvent(Rml::Event& event) override;

private:
    void SyncUIFromConfig();
    void UpdatePreview();
    void UpdateSwatch();
    void UpdateCodeField();
    void SetValueText(const char *id, const char *text);
    void SetPreviewRect(Rml::Element *element, int x, int y, int w, int h);
    void ApplyAction(const Rml::String& action);

    Rml::Context *m_ctx = nullptr;
    Rml::ElementDocument *m_doc = nullptr;
    Rml::Element *m_root = nullptr;
    CrosshairHUD *m_crosshair = nullptr;
    bool m_visible = false;
    bool m_transparent_background = false;
    bool m_close_requested = false;

    Rml::Element *m_pv_t = nullptr;
    Rml::Element *m_pv_b = nullptr;
    Rml::Element *m_pv_l = nullptr;
    Rml::Element *m_pv_r = nullptr;
    Rml::Element *m_pv_dot = nullptr;
    Rml::Element *m_pv_circle = nullptr;
};
