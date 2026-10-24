#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <winrt/Windows.System.h>

struct NightLightState
{
    bool available = false;  // Windows' night light record was found and understood
    bool on = false;
};

// Windows has no public call for night light. Its on/off state is a small
// binary record in the registry (the same one Quick Settings changes):
//
//   43 42 01 00 0A 02 01 00 2A 06 <time stamp> 2A 2B 0E <size> 43 42 01 00 [10 00] ...
//
// "10 00" is there while night light is on, and <size> counts it. These read
// and change that record; one that doesn't look like this is left alone.
// Pure logic, unit-tested.
std::optional<bool> NightLightIsOn(const std::vector<uint8_t>& record);
std::optional<std::vector<uint8_t>> NightLightWithState(const std::vector<uint8_t>& record, bool on, uint64_t unixSeconds);

// Night light (Settings > System > Display > Night light): its state, changes
// made anywhere (Quick Settings, its schedule) through registry change
// notifications (no polling), and switching it. Results arrive on the UI thread.
class NightLightService
{
public:
    NightLightService() = default;
    NightLightService(const NightLightService&) = delete;
    NightLightService& operator=(const NightLightService&) = delete;
    ~NightLightService();

    void Start(std::function<void(const NightLightState&)> onChange);
    // Returns false if Windows' record wasn't understood (then open Settings instead).
    bool Set(bool on);
    const NightLightState& Current() const { return m_state; }

private:
    // One registry key that holds a copy of the record, and its change notification.
    struct Record
    {
        HKEY key{};
        HANDLE changed{};
        PTP_WAIT wait{};
    };

    void Read();
    void Arm(Record& record);
    static void CALLBACK OnChanged(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT);

    std::function<void(const NightLightState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    std::vector<std::unique_ptr<Record>> m_records;
    NightLightState m_state;
};
