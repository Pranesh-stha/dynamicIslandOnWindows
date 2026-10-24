#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <WbemIdl.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.System.h>

#include "services/Throttle.h"



struct DisplayBrightness
{
    std::wstring id;         // stays the same while the display is connected
    std::wstring name;       // "Legion 27Q-10", or "Built-in display" for the laptop panel
    bool internal = false;   // the laptop's own panel (controlled through WMI)
    bool primary = false;
    bool supported = false;  // false: the monitor doesn't answer (DDC/CI off in its menu, PLAN.md E8)
    int value = 0;           // 0..100
};

struct BrightnessState
{
    std::vector<DisplayBrightness> displays;  // the primary monitor first
};

// Brightness of every connected display. The laptop panel goes through WMI
// (50-500 ms per call), external monitors through DDC/CI, the monitor's own
// control channel over the display cable (50-200 ms per call). All of that
// runs on one background thread, so sliders never wait for hardware: only
// the newest value is sent, at most a few times per second (E7, E9).
class BrightnessService
{
public:
    BrightnessService() = default;
    BrightnessService(const BrightnessService&) = delete;
    BrightnessService& operator=(const BrightnessService&) = delete;
    ~BrightnessService();

    // onChange runs on this (the UI) thread.
    void Start(std::function<void(const BrightnessState&)> onChange);

    // Lists the displays again and reads their brightness (in the background).
    void Refresh();

    // value is 0..1. Returns immediately; the hardware follows.
    void Set(const std::wstring& id, float value);

private:
    struct Monitor
    {
        DisplayBrightness info;
        HANDLE physical = nullptr;  // DDC/CI handle for external monitors
        DWORD maximum = 100;
        Throttle throttle{ 250 };
        bool readOnce = false;
    };

    void Run();
    void Enumerate();
    void ReadAll();
    bool Write(Monitor& monitor, int value);
    void Publish();
    void CloseMonitors();
    bool ConnectWmi();
    int ReadWmi();
    bool WriteWmi(int value);

    std::function<void(const BrightnessState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;

    // Shared with the worker thread, guarded by m_mutex.
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_refresh = false;
    std::map<std::wstring, int> m_requests;  // display id -> wanted value

    // Used only on the worker thread.
    std::vector<Monitor> m_monitors;
    winrt::com_ptr<IWbemServices> m_wmi;
    std::wstring m_wmiMethodsPath;
};
