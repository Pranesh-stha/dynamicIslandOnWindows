#pragma once

#include <winrt/Windows.System.h>

#include "services/AudioService.h"
#include "services/NightLightService.h"
#include "services/PowerService.h"
#include "views/sections/Section.h"

// The control center's top line: the time and date on the left, quick
// buttons (microphone, night light, keep awake, sleep) in the middle, the
// battery on the right. The clock only ticks while the panel is open (one wake-up a
// minute); an idle island has no timer at all.
class StatusSection : public Section
{
public:
    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void OnVisibilityChanged(bool visible) override;
    void SetPower(const PowerState& power, bool animate);
    void SetAudio(const AudioState& audio, bool animate);
    void SetNightLight(const NightLightState& nightLight, bool animate);
    void SetKeepAwake(bool on, bool animate);

private:
    void UpdateClock();
    void ArrangeBattery();
    void ArrangeChips();
    void ApplyChips(bool animate);
    void ScheduleTick();

    PowerState m_power;
    AudioState m_audio;
    NightLightState m_nightLight;
    bool m_keepAwake = false;
    float m_x = 0.0f, m_y = 0.0f, m_width = 0.0f;
    std::unique_ptr<Label> m_time, m_date, m_batteryIcon, m_detail;
    std::unique_ptr<RollingText> m_percent;  // rolls when the charge changes
    std::unique_ptr<ToggleChip> m_mic, m_nightLightChip, m_keepAwakeChip, m_sleepChip;
    winrt::Windows::System::DispatcherQueueTimer m_tick{ nullptr };
    bool m_running = false;
};
