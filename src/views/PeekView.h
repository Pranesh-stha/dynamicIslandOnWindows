#pragma once

#include <memory>
#include <vector>

#include "services/MediaState.h"
#include "services/PowerService.h"
#include "services/TimerService.h"
#include "views/Actions.h"
#include "views/View.h"

// The wider strip shown while the mouse rests on the island: the song with
// play/pause and next; when nothing is playing, a glance: the sun's path
// through the day, the time, the date and the battery; while a timer or
// stopwatch is on, the time in big digits with pause and stop instead.
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
    void UpdateClock(bool animate);  // the time, the date, the sun (or moon) on its path
    void ScheduleTick();             // just after the next whole minute, while shown
    void ApplyBattery(bool animate);
    Motion::float3 SunAt(float progress) const;  // the sun's top-left at `progress` along the arc (0 = left horizon)

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
    winrt::Windows::UI::Composition::ContainerVisual m_sky{ nullptr };  // the sun's path: arc, horizon, sun or moon, stars
    winrt::Windows::UI::Composition::ShapeVisual m_arc{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_sun{ nullptr };  // the sun (or moon) and its glow; moves along the arc
    std::unique_ptr<RoundedRect> m_horizon, m_sunGlow, m_sunCore;
    std::vector<std::unique_ptr<RoundedRect>> m_stars;
    std::unique_ptr<Label> m_date, m_batteryDetail, m_bolt;
    std::unique_ptr<RollingText> m_clock, m_batteryPercent;
    std::unique_ptr<RoundedRect> m_batteryBody, m_batteryFill, m_batteryTip;
    winrt::Windows::UI::Composition::ContainerVisual m_battery{ nullptr };
    float m_arcRadius = 0.0f;
    Motion::float2 m_arcCenter{};
    float m_sunProgress = 0.0f;
    int m_skyDay = -1;  // 1 = the sun's colours, 0 = the moon's, -1 = not set yet
    winrt::Windows::System::DispatcherQueueTimer m_tick{ nullptr };
};

// Where the glance's sun (6:00-18:00) or moon (18:00-6:00) is on its arc:
// `progress` 0 at the left horizon, 1 at the right. Pure logic, unit-tested.
struct SunPath
{
    bool day = true;
    float progress = 0.0f;
};
SunPath SunPathAt(int minuteOfDay);  // 0-1439

// The colour of the battery's fill. Pure logic, unit-tested.
enum class BatteryTone { Normal, Charging, Low, Critical };
BatteryTone BatteryToneOf(const PowerState& power);
