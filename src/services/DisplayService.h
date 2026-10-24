#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

// What Win+P calls "project": where the picture goes when a second display
// is connected.
enum class Projection
{
    Unknown,
    PcScreenOnly,      // only the laptop's own screen
    Duplicate,         // the same picture on both
    Extend,            // one big desktop across both
    SecondScreenOnly,  // only the other display
};

struct DisplayInfo
{
    std::wstring id;    // the same ids as BrightnessService's
    std::wstring name;  // "Legion 27Q-10", "Built-in display"
    bool internal = false;
    int width = 0, height = 0;  // pixels
    int refreshHz = 0;
};

struct DisplaysState
{
    std::vector<DisplayInfo> active;  // displays showing something
    int connected = 0;                // plugged in, showing something or not
    Projection projection = Projection::Unknown;
    Projection switchingTo = Projection::Unknown;  // a change is under way
};

// Pure rules, unit-tested.
Projection ProjectionFromTopology(UINT32 topology);  // DISPLAYCONFIG_TOPOLOGY_ID -> Projection
UINT32 TopologyFlag(Projection projection);          // Projection -> SDC_TOPOLOGY_* for SetDisplayConfig
int RefreshHz(UINT32 numerator, UINT32 denominator); // 143.98 -> 144

// Connected displays and the projection mode (Windows' display configuration
// API, the one Win+P uses). Read again whenever displays change; switching
// runs on a background thread, since Windows takes a second or two.
class DisplayService
{
public:
    DisplayService() = default;
    DisplayService(const DisplayService&) = delete;
    DisplayService& operator=(const DisplayService&) = delete;
    ~DisplayService() { m_alive.reset(); }

    void Start(std::function<void(const DisplaysState&)> onChange);
    void Refresh();
    void SetProjection(Projection projection);
    const DisplaysState& Current() const { return m_state; }

private:
    winrt::fire_and_forget SwitchAsync(Projection projection);
    void Publish();

    std::function<void(const DisplaysState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    DisplaysState m_state;
};
