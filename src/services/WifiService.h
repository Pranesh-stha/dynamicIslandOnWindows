#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Windows.Devices.WiFi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>

struct WifiNetwork
{
    std::wstring ssid;       // the network's name
    int bars = 0;            // signal strength, 0..4
    bool secured = false;    // needs a password
    bool saved = false;      // Windows already knows it (and its password)
    bool connected = false;
};

struct WifiState
{
    bool available = false;  // there is a Wi-Fi adapter we may use
    bool scanning = false;
    std::wstring connecting; // the network being joined right now
    std::vector<WifiNetwork> networks;
};

// One row per network name (a home network often has several access points
// or a 2.4 and a 5 GHz band): the strongest signal wins. Hidden networks
// (no name) are left out. Order: connected first, then saved ones, then by
// signal, then by name. Pure logic, unit-tested.
std::vector<WifiNetwork> GroupNetworks(const std::vector<WifiNetwork>& accessPoints, const std::wstring& connectedSsid,
                                       const std::vector<std::wstring>& savedSsids);

// Nearby Wi-Fi networks and switching between them (Windows.Devices.WiFi).
// Nothing runs in the background: the list is refreshed only while it's
// on screen (Scan), so an idle island never wakes the Wi-Fi card.
class WifiService
{
public:
    WifiService() = default;
    WifiService(const WifiService&) = delete;
    WifiService& operator=(const WifiService&) = delete;
    ~WifiService();

    // onChange runs on this (the UI) thread.
    void Start(std::function<void(const WifiState&)> onChange);

    // Asks Windows for fresh results; at most every few seconds.
    void Scan();

    // Joins a network. A saved or open one is joined right away; for a new
    // one with a password, Windows' own network list opens so you can type it
    // (the island never takes keyboard focus).
    void Connect(const std::wstring& ssid);
    void Disconnect();

private:
    winrt::fire_and_forget StartAsync();
    winrt::fire_and_forget ScanAsync();
    winrt::fire_and_forget ConnectAsync(std::wstring ssid);
    winrt::Windows::Foundation::IAsyncAction RefreshAsync();
    void Publish();

    std::function<void(const WifiState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    winrt::Windows::Devices::WiFi::WiFiAdapter m_adapter{ nullptr };
    std::vector<winrt::Windows::Devices::WiFi::WiFiAvailableNetwork> m_seen;  // from the last scan
    uint64_t m_lastScan = 0;
    bool m_scanWanted = false;  // asked for before the adapter was found
    WifiState m_state;
};
