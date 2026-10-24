#pragma once

#include <vector>

#include "services/BrightnessService.h"
#include "views/sections/Section.h"

// The "Displays" column: one brightness slider per connected display, the
// island's own monitor first.
class DisplaysSection : public Section
{
public:
    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;

    // Returns true if displays came or went: then the control center has to
    // be built again (the column's height changes).
    bool SetBrightness(const BrightnessState& brightness, bool animate);

private:
    void Apply(bool animate);

    BrightnessState m_brightness;
    std::vector<SliderRow> m_rows;
    std::vector<std::wstring> m_ids;
    std::unique_ptr<Label> m_hint;
};
