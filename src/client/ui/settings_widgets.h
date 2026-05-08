#pragma once

#include <RmlUi/Core.h>

namespace SettingsWidgets {
Rml::Element *AppendElement(Rml::Element *parent, const char *tag, const char *class_name = nullptr);
Rml::Element *AppendTextElement(Rml::Element *parent, const char *tag, const char *class_name, const char *text);
void SetText(Rml::Element *element, const char *text);
void SetDisplay(Rml::Element *element, bool visible);
void SetPercent(Rml::Element *element, const char *property, float value);
}
