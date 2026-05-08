#include "settings_widgets.h"

#include <cstdio>
#include <string>

namespace SettingsWidgets {

Rml::Element *AppendElement(Rml::Element *parent, const char *tag, const char *class_name)
{
    if (!parent || !tag) {
        return nullptr;
    }
    Rml::ElementPtr element = parent->GetOwnerDocument()->CreateElement(tag);
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

Rml::Element *AppendTextElement(Rml::Element *parent, const char *tag, const char *class_name, const char *text)
{
    Rml::Element *element = AppendElement(parent, tag, class_name);
    SetText(element, text);
    return element;
}

void SetText(Rml::Element *element, const char *text)
{
    if (element) {
        std::string escaped;
        if (text) {
            for (const char *p = text; *p; ++p) {
                switch (*p) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default: escaped.push_back(*p); break;
                }
            }
        }
        element->SetInnerRML(escaped);
    }
}

void SetDisplay(Rml::Element *element, bool visible)
{
    if (element) {
        if (visible) {
            // Remove the inline override so the stylesheet display value takes effect.
            // (Setting display:block inline would break elements styled display:flex.)
            element->RemoveProperty("display");
        } else {
            element->SetProperty("display", "none");
        }
    }
}

void SetPercent(Rml::Element *element, const char *property, float value)
{
    if (!element || !property) {
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", value);
    element->SetProperty(property, buffer);
}

} // namespace SettingsWidgets
