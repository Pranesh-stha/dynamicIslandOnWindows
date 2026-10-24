#pragma once

#include <memory>
#include <string>

#include "services/MediaState.h"
#include "services/TimerService.h"
#include "views/View.h"

// What the small resting pill shows, one of:
//   - while the charger was just plugged in (a few seconds): a battery
//     filling up to the charge, "Charging" and the percentage;
//   - while a timer or stopwatch is on: its icon and the time, rolling;
//   - while media plays: the artwork on the left and a "playing" mark on the right;
//   - else nothing (plain glass).
// Nothing here animates on its own: only when something changes.
class IdleView : public View
{
public:
    using View::View;
    void Build() override;
    void SetMedia(const MediaState& media);
    void SetTimer(const TimerState& timer, const std::wstring& text);
    void SetCharging(bool show, int percent);

private:
    void Apply(bool animate);

    MediaState m_media;
    TimerState m_timer;
    std::wstring m_timerText;
    bool m_charging = false;
    int m_percent = 0;

    winrt::Windows::UI::Composition::ContainerVisual m_mediaLayer{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_bars{ nullptr };
    std::unique_ptr<Artwork> m_art;

    winrt::Windows::UI::Composition::ContainerVisual m_timerLayer{ nullptr };
    std::unique_ptr<RoundedRect> m_timerBadge;
    std::unique_ptr<Label> m_timerIcon, m_timerDone;
    std::unique_ptr<RollingText> m_timerTime;

    winrt::Windows::UI::Composition::ContainerVisual m_chargeLayer{ nullptr };
    std::unique_ptr<RoundedRect> m_batteryBody, m_batteryFill, m_batteryTip;
    std::unique_ptr<Label> m_bolt, m_chargingText;
    std::unique_ptr<RollingText> m_chargePercent;
};
