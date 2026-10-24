#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>

enum class DeviceKind
{
    Other,
    Audio,      // headphones, speakers, headsets
    Mouse,
    Keyboard,
    Phone,
    Computer,
    Gamepad,
};

struct BluetoothDevice
{
    std::wstring id;          // Windows' id for the device (used to connect or pair it)
    std::wstring name;
    DeviceKind kind = DeviceKind::Other;
    bool connected = false;
    bool canConnect = false;  // an audio device: Island can connect and disconnect it
    bool busy = false;        // connecting, disconnecting or pairing right now
    std::wstring detail;      // the line under the name
};

struct BluetoothState
{
    std::vector<BluetoothDevice> paired;  // connected first, then by name
    std::vector<BluetoothDevice> nearby;  // new devices found while searching
    bool searching = false;
};

// The kind of device from its Bluetooth "class of device" (major and minor
// numbers every classic Bluetooth device announces). Pure, unit-tested.
DeviceKind KindFromClassOfDevice(uint32_t major, uint32_t minor);

// Connected first, then alphabetical. Pure, unit-tested.
void SortDevices(std::vector<BluetoothDevice>& devices);

// Bluetooth devices: the paired ones (connected or not), connecting and
// disconnecting audio devices, and finding and pairing new ones.
//
// Paired devices are followed with a Windows "device watcher", which only
// reports changes (no polling). Searching for new devices uses the radio, so
// it runs only while you're looking at the "Add a device" list.
class BluetoothService
{
public:
    BluetoothService() = default;
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;
    ~BluetoothService();

    // onChange runs on this (the UI) thread.
    void Start(std::function<void(const BluetoothState&)> onChange);

    // Connects a paired audio device, or disconnects it if it's connected.
    void Toggle(const std::wstring& id);

    void StartSearch();
    void StopSearch();
    void Pair(const std::wstring& id);

private:
    struct Entry
    {
        winrt::Windows::Devices::Enumeration::DeviceInformation info{ nullptr };
        bool busy = false;
        std::wstring detail;  // overrides the usual detail while busy or after a problem
    };

    using Watcher = winrt::Windows::Devices::Enumeration::DeviceWatcher;
    Watcher CreateWatcher(bool paired);
    void Subscribe(Watcher& watcher, std::map<std::wstring, Entry>& entries);
    winrt::fire_and_forget RefreshAudioDevices();
    winrt::fire_and_forget ToggleAsync(std::wstring id, winrt::guid container, bool connect);
    winrt::fire_and_forget PairAsync(std::wstring id);
    BluetoothDevice Describe(const Entry& entry, bool paired) const;
    void Publish();

    std::function<void(const BluetoothState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    CO_MTA_USAGE_COOKIE m_mtaCookie{};

    Watcher m_pairedWatcher{ nullptr };
    Watcher m_searchWatcher{ nullptr };
    std::vector<winrt::event_token> m_pairedTokens, m_searchTokens;
    std::map<std::wstring, Entry> m_paired;
    std::map<std::wstring, Entry> m_nearby;
    std::set<std::wstring> m_audioContainers;  // container ids (as text) that have speakers or a microphone
    bool m_searching = false;
    bool m_audioLookupRunning = false;
    bool m_audioLookupAgain = false;  // devices changed during the lookup: look again when it ends
};
