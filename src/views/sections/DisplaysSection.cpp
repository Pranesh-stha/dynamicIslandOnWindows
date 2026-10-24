#include "views/sections/DisplaysSection.h"

#include <format>

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace
{
    struct Mode
    {
        Projection projection;
        wchar_t glyph;
        const wchar_t* title;
    };

    constexpr Mode kModeList[DisplaysSection::kModes] = {
        { Projection::PcScreenOnly, Glyph::Laptop, L"PC screen only" },
        { Projection::Duplicate, Glyph::Duplicate, L"Duplicate" },
        { Projection::Extend, Glyph::Extend, L"Extend" },
        { Projection::SecondScreenOnly, Glyph::Monitor, L"Second screen only" },
    };
}

std::wstring DisplayCaption(const DisplayBrightness& display, const DisplaysState& displays)
{
    // A monitor that doesn't answer gets a hint instead (PLAN.md E8).
    if (!display.supported)
    {
        return display.name + L" · turn on DDC/CI in its menu";
    }
    for (const DisplayInfo& info : displays.active)
    {
        if (info.id == display.id && info.refreshHz > 0)
        {
            return std::format(L"{} · {} Hz", display.name, info.refreshHz);
        }
    }
    return display.name;
}

float DisplaysSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    y = AddTitle(layer, L"Displays", x, y);
    m_rows.clear();
    m_names.clear();
    m_ids.clear();
    m_rows.resize(m_brightness.displays.size());
    for (size_t i = 0; i < m_brightness.displays.size(); ++i)
    {
        const std::wstring id = m_brightness.displays[i].id;
        m_ids.push_back(id);
        SliderRow& row = m_rows[i];
        const float rowTop = y;
        y = row.Build(m_ui, layer, controls, x, y, width, Glyph::Brightness);
        row.icon->SetInteractive(false);
        row.slider->onChange = [this, id](float value, bool) { if (m_actions.setBrightness) m_actions.setBrightness(id, value); };
        row.slider->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:display"); };

        // The display's name, clickable: Settings › Display. It stands where
        // the row's own caption would.
        auto name = std::make_unique<TextButton>(m_ui, 11.0f);
        name->SetTextOpacity(0.55f);
        name->onClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:display"); };
        const float captionX = row.slider->X();
        m_captionWidth = x + width - captionX + m_ui.Px(12.0f);
        name->SetBounds(captionX - m_ui.Px(6.0f), rowTop - m_ui.Px(1.0f), 1.0f, 1.0f);
        Add(layer, controls, *name);
        m_names.push_back(std::move(name));
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

    // Where the picture goes, once a second display is plugged in.
    m_builtWithProjection = ShowsProjection();
    for (auto& mode : m_modes)
    {
        mode.reset();
    }
    if (m_builtWithProjection)
    {
        y = AddTitle(layer, L"Project", x, y + m_ui.Px(4.0f));
        const float rowHeight = m_ui.Px(32.0f);
        for (int i = 0; i < kModes; ++i)
        {
            m_modes[i] = std::make_unique<ListRow>(m_ui);
            m_modes[i]->SetBounds(x, y, width, rowHeight);
            const Projection projection = kModeList[i].projection;
            m_modes[i]->onClick = [this, projection] { if (m_actions.setProjection) m_actions.setProjection(projection); };
            m_modes[i]->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:display"); };
            Add(layer, controls, *m_modes[i]);
            y += rowHeight + m_ui.Px(1.0f);
        }
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

bool DisplaysSection::SetDisplays(const DisplaysState& displays, bool animate)
{
    m_displays = displays;
    if (ShowsProjection() != m_builtWithProjection)
    {
        return true;
    }
    Apply(animate);
    return false;
}

void DisplaysSection::OnVisibilityChanged(bool visible)
{
    m_visible = visible;
    Apply(false);  // stops the "switching" pulse while hidden
}

void DisplaysSection::Apply(bool animate)
{
    for (size_t i = 0; i < m_rows.size() && i < m_brightness.displays.size(); ++i)
    {
        const DisplayBrightness& display = m_brightness.displays[i];
        SliderRow& row = m_rows[i];
        row.caption->SetText(L"");  // the clickable name replaces it
        m_names[i]->SetText(DisplayCaption(display, m_displays), m_captionWidth);
        row.slider->SetEnabled(display.supported);
        row.icon->SetEnabled(display.supported);
        if (display.supported)
        {
            row.slider->SetValue(display.value / 100.0f, animate);
        }
    }
    for (int i = 0; i < kModes; ++i)
    {
        if (!m_modes[i])
        {
            continue;
        }
        const Projection projection = kModeList[i].projection;
        const bool current = m_displays.projection == projection;
        m_modes[i]->SetContent(kModeList[i].glyph, kModeList[i].title, L"", 0, current, animate);
        m_modes[i]->SetBusy(m_visible && m_displays.switchingTo == projection);
        m_modes[i]->SetInteractive(!current && m_displays.switchingTo == Projection::Unknown);
    }
}
