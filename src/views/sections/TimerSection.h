#pragma once

#include <array>
#include <string>

#include "services/TimerService.h"
#include "views/sections/Section.h"

// "Timer", under Now playing: start a 5, 10 or 25 minute timer or the
// stopwatch; while one is on, the time (rolling), pause/resume, +1 min and stop.
class TimerSection : public Section
{
public:
    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void SetTimer(const TimerState& timer, const std::wstring& text, bool animate);

private:
    void Apply(bool animate);

    TimerState m_timer;
    std::wstring m_text;
    float m_x = 0.0f, m_rowY = 0.0f, m_rowHeight = 0.0f;
    std::array<std::unique_ptr<ToggleChip>, 4> m_starts;  // 5, 10, 25 min, stopwatch
    std::unique_ptr<RollingText> m_time;
    std::unique_ptr<Label> m_caption;
    std::unique_ptr<ToggleChip> m_pause, m_addMinute, m_stop;
};
