#pragma once

#include <winrt/Windows.System.h>

#include "services/PowerService.h"
#include "views/sections/Section.h"

// The control center's top line: the time and date on the left, the
// battery on the right. The clock only ticks while the panel is open (one
// wake-up a minute); an idle island has no timer at all.
class StatusSection : public Section
{
public:
    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void OnVisibilityChanged(bool visible) override;
    void SetPower(const PowerState& power);

private:
    void UpdateClock();
    void ArrangeBattery();
    void ScheduleTick();

    PowerState m_power;
    float m_x = 0.0f, m_y = 0.0f, m_width = 0.0f;
    std::unique_ptr<Label> m_time, m_date, m_batteryIcon, m_percent, m_detail;
    winrt::Windows::System::DispatcherQueueTimer m_tick{ nullptr };
    bool m_running = false;
};
