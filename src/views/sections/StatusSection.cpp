#include "views/sections/StatusSection.h"

#include <chrono>
#include <format>

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

float StatusSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    m_x = x;
    m_y = y;
    m_width = width;
    m_time = std::make_unique<Label>(m_ui, 24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_date = std::make_unique<Label>(m_ui, 13.0f);
    m_date->Visual().Opacity(0.6f);
    m_batteryIcon = std::make_unique<Label>(m_ui, 17.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_percent = std::make_unique<RollingText>(m_ui, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_detail = std::make_unique<Label>(m_ui, 12.5f);
    m_detail->Visual().Opacity(0.6f);
    for (Label* label : { m_time.get(), m_date.get(), m_batteryIcon.get(), m_detail.get() })
    {
        layer.Children().InsertAtTop(label->Visual());
    }
    layer.Children().InsertAtTop(m_percent->Visual());

    // Quick toggles. A muted microphone shows in red, like a call's mute button.
    m_mic = std::make_unique<ToggleChip>(m_ui, Glyph::Mic, L"Microphone");
    m_mic->SetOnColor({ 255, 235, 72, 64 });
    m_mic->onClick = [this] { if (m_actions.toggleMicMute) m_actions.toggleMicMute(); };
    m_mic->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:sound"); };
    m_nightLightChip = std::make_unique<ToggleChip>(m_ui, Glyph::NightLight, L"Night light");
    m_nightLightChip->onClick = [this]
    {
        if (!m_nightLight.available)
        {
            if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:nightlight");
        }
        else if (m_actions.setNightLight)
        {
            m_actions.setNightLight(!m_nightLight.on);
        }
    };
    m_nightLightChip->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:nightlight"); };
    // Keeps the PC from sleeping and the screen on (PowerToys Awake style).
    m_keepAwakeChip = std::make_unique<ToggleChip>(m_ui, Glyph::KeepAwake, L"Keep awake");
    m_keepAwakeChip->onClick = [this] { if (m_actions.setKeepAwake) m_actions.setKeepAwake(!m_keepAwake); };
    m_keepAwakeChip->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:powersleep"); };
    // Sleep now: the colour blooms as a "going" sign, and the PC sleeps a moment later.
    m_sleepChip = std::make_unique<ToggleChip>(m_ui, Glyph::Sleep, L"Sleep");
    m_sleepChip->onClick = [this]
    {
        m_sleepChip->SetOn(true, m_running);
        if (m_actions.sleepNow) m_actions.sleepNow();
    };
    m_sleepChip->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:powersleep"); };
    for (ToggleChip* chip : { m_mic.get(), m_nightLightChip.get(), m_keepAwakeChip.get(), m_sleepChip.get() })
    {
        Add(layer, controls, *chip);
    }

    UpdateClock();
    ArrangeBattery();
    ArrangeChips();
    ApplyChips(false);
    if (m_running)
    {
        ScheduleTick();
    }
    return y + m_time->Height();
}

void StatusSection::ArrangeChips()
{
    // Centred on the panel, on the clock's line.
    const float height = m_ui.EvenPx(32.0f);
    const float gap = m_ui.Px(8.0f);
    float total = -gap;
    for (ToggleChip* chip : { m_mic.get(), m_nightLightChip.get(), m_keepAwakeChip.get(), m_sleepChip.get() })
    {
        total += chip->NaturalWidth() + gap;
    }
    float x = std::round(m_x + (m_width - total) / 2.0f);
    const float y = std::round(m_y + (m_time->Height() - height) / 2.0f);
    for (ToggleChip* chip : { m_mic.get(), m_nightLightChip.get(), m_keepAwakeChip.get(), m_sleepChip.get() })
    {
        chip->SetBounds(x, y, chip->NaturalWidth(), height);
        x += chip->NaturalWidth() + gap;
    }
}

void StatusSection::ApplyChips(bool animate)
{
    if (!m_mic)
    {
        return;
    }
    m_mic->SetEnabled(m_audio.micAvailable);
    m_mic->SetGlyph(m_audio.micMuted ? Glyph::MicOff : Glyph::Mic);
    m_mic->SetOn(m_audio.micMuted, animate);
    m_nightLightChip->SetOn(m_nightLight.available && m_nightLight.on, animate);
    m_keepAwakeChip->SetOn(m_keepAwake, animate);
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

void StatusSection::SetPower(const PowerState& power, bool animate)
{
    m_power = power;
    if (m_time)
    {
        ArrangeBattery();
        ApplyChips(animate);
    }
}

void StatusSection::SetAudio(const AudioState& audio, bool animate)
{
    m_audio = audio;
    ApplyChips(animate);
}

void StatusSection::SetNightLight(const NightLightState& nightLight, bool animate)
{
    m_nightLight = nightLight;
    ApplyChips(animate);
}

void StatusSection::SetKeepAwake(bool on, bool animate)
{
    m_keepAwake = on;
    ApplyChips(animate);
}

void StatusSection::ArrangeBattery()
{
    // Right-aligned: [icon] 84 %  Charging · 45 min to full
    m_batteryIcon->SetText(std::wstring(1, BatteryGlyph(m_power)));
    m_percent->SetText(m_power.hasBattery ? std::format(L"{} %", m_power.percent) : L"", m_running);
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
    else
    {
        if (m_tick)
        {
            m_tick.Stop();
        }
        if (m_sleepChip)
        {
            m_sleepChip->SetOn(false, false);  // ready again after waking
        }
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
