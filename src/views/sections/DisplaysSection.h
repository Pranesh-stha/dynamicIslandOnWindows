#pragma once

#include <array>
#include <vector>

#include "services/BrightnessService.h"
#include "services/DisplayService.h"
#include "views/sections/Section.h"

// The "Displays" column: one brightness slider per connected display, the
// island's own monitor first, each named (click the name for Settings ›
// Display); and, with a second display plugged in, where the picture goes
// (PC screen only / Duplicate / Extend / Second screen only, like Win+P).
class DisplaysSection : public Section
{
public:
    static constexpr int kModes = 4;

    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void OnVisibilityChanged(bool visible) override;

    // Both return true if the column changes shape (a display came or went):
    // then the control center has to be built again.
    bool SetBrightness(const BrightnessState& brightness, bool animate);
    bool SetDisplays(const DisplaysState& displays, bool animate);

private:
    void Apply(bool animate);
    bool ShowsProjection() const { return m_displays.connected >= 2; }

    BrightnessState m_brightness;
    DisplaysState m_displays;
    bool m_visible = false;
    bool m_builtWithProjection = false;
    float m_captionWidth = 0.0f;
    std::vector<SliderRow> m_rows;
    std::vector<std::unique_ptr<TextButton>> m_names;
    std::vector<std::wstring> m_ids;
    std::unique_ptr<Label> m_hint;
    std::array<std::unique_ptr<ListRow>, kModes> m_modes;
};

// "Legion 27Q-10 · 144 Hz". Pure, unit-tested.
std::wstring DisplayCaption(const DisplayBrightness& display, const DisplaysState& displays);
