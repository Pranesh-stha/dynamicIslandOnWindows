#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "render/Widgets.h"
#include "views/Actions.h"

// One part of the control center: the header with the clock, or one of its
// columns. A section builds its visuals into the control center's layer
// and registers its controls with the control center's ControlHost.
class Section
{
public:
    Section(const Ui& ui, const IslandActions& actions) : m_ui(ui), m_actions(actions) {}
    virtual ~Section() = default;
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;

    // Builds the section at (x, y), `width` pixels wide. Returns the y where it ends.
    virtual float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                        float x, float y, float width) = 0;

    // The control center opened or closed (e.g. start or stop a clock or a scan).
    virtual void OnVisibilityChanged(bool /*visible*/) {}

    // Forgets the visuals of the last Build (called before building again).
    void Reset() { m_titles.clear(); }

protected:
    // A small, dim title above a column, e.g. "Sound". Returns the y below it.
    float AddTitle(const winrt::Windows::UI::Composition::ContainerVisual& layer, std::wstring_view text, float x, float y)
    {
        auto title = std::make_unique<Label>(m_ui, 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        title->SetText(text);
        title->Visual().Opacity(0.45f);
        title->SetPosition(x, y);
        layer.Children().InsertAtTop(title->Visual());
        const float bottom = y + title->Height() + m_ui.Px(6.0f);
        m_titles.push_back(std::move(title));
        return bottom;
    }

    static void Add(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls, Control& control)
    {
        layer.Children().InsertAtTop(control.Visual());
        controls.Add(control);
    }

    const Ui& m_ui;
    const IslandActions& m_actions;

private:
    std::vector<std::unique_ptr<Label>> m_titles;
};

// A slider with a caption above it and an icon button to its left (volume,
// an app's volume, a display's brightness).
struct SliderRow
{
    std::unique_ptr<Label> caption;
    std::unique_ptr<IconButton> icon;
    std::unique_ptr<Slider> slider;

    // Returns the y below the row.
    float Build(const Ui& ui, const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width, wchar_t glyph)
    {
        const float iconSize = ui.EvenPx(30.0f);
        const float sliderX = x + iconSize + ui.Px(8.0f);
        const float sliderHeight = ui.EvenPx(22.0f);  // even, like the bar inside it, so the bar sits on whole pixels
        caption = std::make_unique<Label>(ui, 11.0f);
        caption->SetMaxWidth(x + width - sliderX);
        caption->Visual().Opacity(0.55f);
        caption->SetText(L" ");
        caption->SetPosition(sliderX, y);
        layer.Children().InsertAtTop(caption->Visual());

        const float lineY = y + caption->Height() + ui.Px(1.0f);
        slider = std::make_unique<Slider>(ui);
        slider->SetBounds(sliderX, lineY, x + width - sliderX, sliderHeight);
        icon = std::make_unique<IconButton>(ui, glyph, 30.0f, 14.0f);
        icon->SetBounds(x, lineY + std::round((sliderHeight - iconSize) / 2.0f), iconSize, iconSize);
        layer.Children().InsertAtTop(icon->Visual());
        layer.Children().InsertAtTop(slider->Visual());
        controls.Add(*icon);
        controls.Add(*slider);
        return lineY + sliderHeight + ui.Px(8.0f);
    }

    // Unused rows are invisible and ignore the mouse.
    void SetShown(bool shown)
    {
        const float opacity = shown ? 1.0f : 0.0f;
        for (const winrt::Windows::UI::Composition::Visual& visual : { winrt::Windows::UI::Composition::Visual(caption->Visual()),
                                                                     winrt::Windows::UI::Composition::Visual(icon->Visual()),
                                                                     winrt::Windows::UI::Composition::Visual(slider->Visual()) })
        {
            visual.StopAnimation(L"Opacity");  // a running enable/disable fade would bring it back
            visual.Opacity(opacity);
        }
        icon->SetInteractive(shown);
        slider->SetInteractive(shown);
    }
};
