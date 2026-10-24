#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <functional>
#include <memory>
#include <string>

#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>

struct RadioInfo
{
    bool available = false;  // this radio exists
    bool switchable = false; // false: switched off in Windows itself (airplane mode, disabled adapter)
    bool on = false;         // what the toggle shows; flips at once when clicked
    bool busy = false;       // Windows is still switching it
    std::wstring detail;     // network name, "On", "Off", ...
};

struct RadiosState
{
    RadioInfo wifi;
    RadioInfo bluetooth;
};

// Wi-Fi and Bluetooth on/off (Windows.Devices.Radios). Switching takes
// Windows a second or two, so a click shows the new state at once with a
// "working" pulse, rapid clicks are merged, and the toggle springs back only
// if Windows refuses (PLAN.md D8). Results arrive on the UI thread.
class RadioService
{
public:
    RadioService() = default;
    RadioService(const RadioService&) = delete;
    RadioService& operator=(const RadioService&) = delete;
    ~RadioService();

    void Start(std::function<void(const RadiosState&)> onChange);
    void SetWifi(bool on) { Set(m_wifi, on); }
    void SetBluetooth(bool on) { Set(m_bluetooth, on); }

private:
    struct Entry
    {
        winrt::Windows::Devices::Radios::Radio radio{ nullptr };
        winrt::event_token token{};
        bool wanted = false;
        bool inFlight = false;
        bool wifi = false;
    };

    winrt::fire_and_forget StartAsync();
    winrt::fire_and_forget Apply(Entry& entry);
    winrt::fire_and_forget ReadNetworkName();
    void Set(Entry& entry, bool on);
    void Read(Entry& entry);
    void Publish();

    std::function<void(const RadiosState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    Entry m_wifi;
    Entry m_bluetooth;
    winrt::event_token m_networkToken{};
    bool m_wifiConnected = false;
    std::wstring m_networkName;  // may stay empty while connected (E10)
    RadiosState m_state;
};
