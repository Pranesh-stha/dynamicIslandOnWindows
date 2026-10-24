#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <powrprof.h>

#include <functional>
#include <memory>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

struct PowerState
{
    bool hasBattery = false;
    int percent = 0;
    bool charging = false;    // the battery is filling up
    bool pluggedIn = false;   // on mains power (charging or full)
    int minutesLeft = -1;     // on battery: Windows' estimate (-1 = unknown)
    int minutesToFull = -1;   // charging: estimated from the charge rate (-1 = unknown)
    bool energySaver = false;
    bool energySaverAvailable = false;  // Windows offers energy saver right now
};

// "Charging · 45 min to full", "2 h 10 min left", "Fully charged", ...
std::wstring PowerDescription(const PowerState& power);

// The battery icon (with a bolt while charging), filled to the nearest 10 %.
wchar_t BatteryGlyph(const PowerState& power);

// Battery level and charging (Windows.System.Power). Event-driven: Windows
// says when anything changes; results arrive on the UI thread.
class PowerService
{
public:
    PowerService() = default;
    PowerService(const PowerService&) = delete;
    PowerService& operator=(const PowerService&) = delete;
    ~PowerService();

    void Start(std::function<void(const PowerState&)> onChange);
    const PowerState& Current() const { return m_state; }

private:
    void Read();
    static ULONG CALLBACK OnSaverStatus(PVOID context, ULONG type, PVOID setting);

    std::function<void(const PowerState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    winrt::event_token m_percentToken{}, m_batteryToken{}, m_supplyToken{}, m_timeToken{}, m_saverToken{}, m_reportToken{};
    // Energy saver's own status (GUID_ENERGY_SAVER_STATUS): unlike the older
    // one, it's right while plugged in too. -1 until Windows has reported it.
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS m_saverParams{};
    HPOWERNOTIFY m_saverNotify{};
    int m_saverStatus = -1;
    PowerState m_state;
};
