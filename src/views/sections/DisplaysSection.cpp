#include "views/sections/DisplaysSection.h"

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

float DisplaysSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    y = AddTitle(layer, L"Displays", x, y);
    m_rows.clear();
    m_ids.clear();
    m_rows.resize(m_brightness.displays.size());
    for (size_t i = 0; i < m_brightness.displays.size(); ++i)
    {
        const std::wstring id = m_brightness.displays[i].id;
        m_ids.push_back(id);
        SliderRow& row = m_rows[i];
        y = row.Build(m_ui, layer, controls, x, y, width, Glyph::Brightness);
        row.icon->SetInteractive(false);
        row.slider->onChange = [this, id](float value, bool) { if (m_actions.setBrightness) m_actions.setBrightness(id, value); };
        row.slider->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:display"); };
    }
    m_hint = std::make_unique<Label>(m_ui, 12.0f);
    m_hint->SetMaxWidth(width);
    m_hint->SetText(m_brightness.displays.empty() ? L"Looking for displays…" : L"");
    m_hint->Visual().Opacity(0.5f);
    m_hint->SetPosition(x, y);
    layer.Children().InsertAtTop(m_hint->Visual());
    if (m_brightness.displays.empty())
    {
        y += m_hint->Height();
    }
    Apply(false);
    return y;
}

bool DisplaysSection::SetBrightness(const BrightnessState& brightness, bool animate)
{
    bool same = brightness.displays.size() == m_ids.size();
    for (size_t i = 0; same && i < m_ids.size(); ++i)
    {
        same = brightness.displays[i].id == m_ids[i];
    }
    m_brightness = brightness;
    if (!same)
    {
        return true;
    }
    Apply(animate);
    return false;
}

void DisplaysSection::Apply(bool animate)
{
    for (size_t i = 0; i < m_rows.size() && i < m_brightness.displays.size(); ++i)
    {
        const DisplayBrightness& display = m_brightness.displays[i];
        SliderRow& row = m_rows[i];
        // A monitor that doesn't answer gets a hint instead (PLAN.md E8).
        row.caption->SetText(display.supported ? display.name : display.name + L" · turn on DDC/CI in its menu");
        row.slider->SetEnabled(display.supported);
        row.icon->SetEnabled(display.supported);
        if (display.supported)
        {
            row.slider->SetValue(display.value / 100.0f, animate);
        }
    }
}
