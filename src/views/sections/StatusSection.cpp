#include "views/sections/StatusSection.h"

#include <chrono>
#include <format>

namespace wuc = winrt::Windows::UI::Composition;

float StatusSection::Build(const wuc::ContainerVisual& layer, ControlHost&, float x, float y, float width)
{
    m_x = x;
    m_y = y;
    m_width = width;
    m_time = std::make_unique<Label>(m_ui, 24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_date = std::make_unique<Label>(m_ui, 13.0f);
    m_date->Visual().Opacity(0.6f);
    m_batteryIcon = std::make_unique<Label>(m_ui, 17.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_percent = std::make_unique<Label>(m_ui, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_detail = std::make_unique<Label>(m_ui, 12.5f);
    m_detail->Visual().Opacity(0.6f);
    for (Label* label : { m_time.get(), m_date.get(), m_batteryIcon.get(), m_percent.get(), m_detail.get() })
    {
        layer.Children().InsertAtTop(label->Visual());
    }
    UpdateClock();
    ArrangeBattery();
    if (m_running)
    {
        ScheduleTick();
    }
    return y + m_time->Height();
}

void StatusSection::UpdateClock()
{
    // In your own format (Settings > Time & language): "08:42" or "8:42 AM".
    wchar_t time[64]{};
    wchar_t date[128]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, time, ARRAYSIZE(time));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, L"dddd, d MMMM", date, ARRAYSIZE(date), nullptr);
    m_time->SetText(time);
    m_date->SetText(date);
    m_time->SetPosition(m_x, m_y);
    const float middle = m_y + m_time->Height() / 2.0f;
    m_date->SetPosition(m_x + m_time->Width() + m_ui.Px(12.0f), std::round(middle - m_date->Height() / 2.0f + m_ui.Px(1.0f)));
}

void StatusSection::SetPower(const PowerState& power)
{
    m_power = power;
    if (m_time)
    {
        ArrangeBattery();
    }
}

void StatusSection::ArrangeBattery()
{
    // Right-aligned: [icon] 84 %  Charging · 45 min to full
    m_batteryIcon->SetText(std::wstring(1, BatteryGlyph(m_power)));
    m_percent->SetText(m_power.hasBattery ? std::format(L"{} %", m_power.percent) : L"");
    m_detail->SetText(PowerDescription(m_power));
    const float middle = m_y + m_time->Height() / 2.0f;
    float right = m_x + m_width;
    m_detail->SetPosition(right - m_detail->Width(), std::round(middle - m_detail->Height() / 2.0f));
    right -= m_detail->Width() + m_ui.Px(10.0f);
    m_percent->SetPosition(right - m_percent->Width(), std::round(middle - m_percent->Height() / 2.0f));
    right -= m_percent->Width() + (m_percent->Text().empty() ? 0.0f : m_ui.Px(6.0f));
    m_batteryIcon->SetPosition(right - m_batteryIcon->Width(), std::round(middle - m_batteryIcon->Height() / 2.0f));
}

void StatusSection::OnVisibilityChanged(bool visible)
{
    m_running = visible;
    if (visible)
    {
        UpdateClock();
        ScheduleTick();
    }
    else if (m_tick)
    {
        m_tick.Stop();
    }
}

void StatusSection::ScheduleTick()
{
    if (!m_tick)
    {
        m_tick = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_tick.IsRepeating(false);
        m_tick.Tick([this](auto&&, auto&&)
        {
            if (m_running)
            {
                UpdateClock();
                ScheduleTick();
            }
        });
    }
    // Wake up just after the next whole minute.
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const int untilNextMinute = (60 - now.wSecond) * 1000 - now.wMilliseconds + 50;
    m_tick.Stop();
    m_tick.Interval(std::chrono::milliseconds(untilNextMinute));
    m_tick.Start();
}
