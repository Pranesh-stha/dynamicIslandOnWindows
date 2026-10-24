#include "views/sections/TimerSection.h"

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

float TimerSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    const Ui& ui = m_ui;
    y = AddTitle(layer, L"Timer", x, y);
    m_x = x;
    m_rowY = y;
    m_rowHeight = ui.EvenPx(30.0f);
    const float gap = ui.Px(6.0f);

    // Not running: the presets and the stopwatch.
    const int minutes[] = { 5, 10, 25 };
    float chipX = x;
    for (int i = 0; i < 4; ++i)
    {
        auto chip = i < 3 ? std::make_unique<ToggleChip>(ui, wchar_t{ 0 }, std::to_wstring(minutes[i]) + L" min")
                          : std::make_unique<ToggleChip>(ui, Glyph::Stopwatch, L"");
        const int length = i < 3 ? minutes[i] : 0;
        chip->onClick = [this, length]
        {
            if (length > 0 && m_actions.startTimer) m_actions.startTimer(length);
            if (length == 0 && m_actions.startStopwatch) m_actions.startStopwatch();
        };
        chip->SetBounds(chipX, y, chip->NaturalWidth(), m_rowHeight);
        chipX += chip->NaturalWidth() + gap;
        Add(layer, controls, *chip);
        m_starts[i] = std::move(chip);
    }

    // Running: the time and its buttons, in the same row.
    m_time = std::make_unique<RollingText>(ui, 17.0f);
    layer.Children().InsertAtTop(m_time->Visual());
    m_caption = std::make_unique<Label>(ui, 11.0f);
    m_caption->Visual().Opacity(0.6f);
    layer.Children().InsertAtTop(m_caption->Visual());
    m_pause = std::make_unique<ToggleChip>(ui, Glyph::Pause, L"");
    m_pause->onClick = [this] { if (m_actions.pauseResumeTimer) m_actions.pauseResumeTimer(); };
    m_addMinute = std::make_unique<ToggleChip>(ui, wchar_t{ 0 }, L"+1 min");
    m_addMinute->onClick = [this] { if (m_actions.addTimerMinute) m_actions.addTimerMinute(); };
    m_stop = std::make_unique<ToggleChip>(ui, Glyph::Cancel, L"");
    m_stop->onClick = [this] { if (m_actions.stopTimer) m_actions.stopTimer(); };
    // Right-aligned: [pause] [+1 min] [stop].
    float right = x + width;
    for (ToggleChip* chip : { m_stop.get(), m_addMinute.get(), m_pause.get() })
    {
        right -= chip->NaturalWidth();
        chip->SetBounds(right, y, chip->NaturalWidth(), m_rowHeight);
        right -= gap;
        Add(layer, controls, *chip);
    }

    Apply(false);
    return y + m_rowHeight;
}

void TimerSection::SetTimer(const TimerState& timer, const std::wstring& text, bool animate)
{
    m_timer = timer;
    m_text = text;
    if (m_time)
    {
        Apply(animate);
    }
}

void TimerSection::Apply(bool animate)
{
    const bool on = m_timer.mode != TimerMode::None;
    auto show = [&](const wuc::Visual& visual, bool shown)
    {
        if (animate)
        {
            Motion::EaseTo(visual, L"Opacity", shown ? 1.0f : 0.0f, 180);
        }
        else
        {
            visual.StopAnimation(L"Opacity");
            visual.Opacity(shown ? 1.0f : 0.0f);
        }
    };
    for (auto& chip : m_starts)
    {
        show(chip->Visual(), !on);
        chip->SetInteractive(!on);
    }
    const bool countdown = m_timer.mode == TimerMode::Countdown;
    show(m_time->Visual(), on);
    show(m_caption->Visual(), on);
    show(m_pause->Visual(), on && !m_timer.finished);
    m_pause->SetInteractive(on && !m_timer.finished);
    show(m_addMinute->Visual(), on && countdown && !m_timer.finished);
    m_addMinute->SetInteractive(on && countdown && !m_timer.finished);
    show(m_stop->Visual(), on);
    m_stop->SetInteractive(on);
    if (!on)
    {
        return;
    }
    m_pause->SetGlyph(m_timer.running ? Glyph::Pause : Glyph::Play);
    m_time->SetText(m_timer.finished ? L"0:00" : m_text, animate);
    m_caption->SetText(m_timer.finished ? L"Time's up" : !m_timer.running ? L"Paused" : countdown ? L"left" : L"stopwatch");
    const float middle = m_rowY + m_rowHeight / 2.0f;
    m_time->SetPosition(m_x, std::round(middle - m_time->Height() / 2.0f));
    m_caption->SetPosition(m_x + m_time->Width() + m_ui.Px(6.0f), std::round(middle - m_caption->Height() / 2.0f + m_ui.Px(1.0f)));
}
