#pragma once

#include <memory>

#include "services/MediaState.h"
#include "services/PowerService.h"
#include "services/TimerService.h"
#include "views/Actions.h"
#include "views/View.h"

// The wider strip shown while the mouse rests on the island: the song with
// play/pause and next; when nothing is playing, a glance at the time of day,
// the date and the battery; while a timer or stopwatch is on, the time in big
// digits with pause and stop instead.
class PeekView : public View
{
public:
    PeekView(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)), m_actions(actions) {}

    void Build() override;
    void SetMedia(const MediaState& media);
    void SetTimer(const TimerState& timer, const std::wstring& text);
    void SetPower(const PowerState& power);

protected:
    void OnVisibilityChanged(bool visible) override;
    void OnAppear() override;

private:
    void Apply(bool animate);
    bool ShowsGlance() const;
    void UpdateClock(bool animate);  // the time, the date, the sky badge
    void ScheduleTick();             // just after the next whole minute, while shown
    void ApplyBattery(bool animate);

    const IslandActions& m_actions;
    MediaState m_media;
    TimerState m_timer;
    PowerState m_power;
    std::wstring m_timerText;
    winrt::Windows::UI::Composition::ContainerVisual m_glance{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_playing{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_timerLayer{ nullptr };
    std::unique_ptr<RoundedRect> m_timerBadge;
    std::unique_ptr<Label> m_timerIcon, m_timerCaption;
    std::unique_ptr<RollingText> m_timerTime;
    std::unique_ptr<IconButton> m_timerPause, m_timerStop;
    std::unique_ptr<Label> m_title, m_subtitle;
    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<IconButton> m_playPause, m_next;

    // The glance (nothing playing).
    winrt::Windows::UI::Composition::ContainerVisual m_sky{ nullptr };  // the time-of-day badge: circle + sun or moon
    std::unique_ptr<RoundedRect> m_skyCircle;
    std::unique_ptr<Label> m_skyIcon, m_date, m_batteryDetail, m_bolt;
    std::unique_ptr<RollingText> m_clock, m_batteryPercent;
    std::unique_ptr<RoundedRect> m_batteryBody, m_batteryFill, m_batteryTip;
    winrt::Windows::UI::Composition::ContainerVisual m_battery{ nullptr };
    int m_skyShown = -1;  // the TimeOfDay the badge shows
    winrt::Windows::System::DispatcherQueueTimer m_tick{ nullptr };
};

// The part of the day the glance's badge shows. Pure logic, unit-tested.
enum class TimeOfDay { Dawn, Day, Dusk, Night };
TimeOfDay TimeOfDayAt(int hour);  // hour 0-23

// The colour of the battery's fill. Pure logic, unit-tested.
enum class BatteryTone { Normal, Charging, Low, Critical };
BatteryTone BatteryToneOf(const PowerState& power);
