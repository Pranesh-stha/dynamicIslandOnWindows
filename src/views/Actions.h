#pragma once

#include <functional>
#include <string>

#include "services/DisplayService.h"

// What the views can ask the rest of Island to do. App fills these in, so
// the views never talk to services directly.
struct IslandActions
{
    // Now playing
    std::function<void()> playPause;
    std::function<void()> next;
    std::function<void()> previous;
    std::function<void()> nextMediaSession;  // when several apps play media

    // Sound
    std::function<void(float volume)> setVolume;  // 0..1
    std::function<void()> toggleMute;
    std::function<void(const std::wstring& app, float volume)> setAppVolume;
    std::function<void(const std::wstring& app)> toggleAppMute;
    std::function<void()> toggleMicMute;

    // Quick toggles
    std::function<void(bool on)> setNightLight;
    std::function<void(bool on)> setKeepAwake;

    // Displays
    std::function<void(const std::wstring& display, float value)> setBrightness;  // 0..1
    std::function<void(Projection projection)> setProjection;                    // like Win+P

    // Connections
    std::function<void(bool on)> setWifi;
    std::function<void(bool on)> setBluetooth;
    std::function<void()> scanWifi;
    std::function<void(const std::wstring& ssid)> connectWifi;
    std::function<void(const std::wstring& device)> toggleBluetoothDevice;
    std::function<void()> startBluetoothSearch;
    std::function<void()> stopBluetoothSearch;
    std::function<void(const std::wstring& device)> pairBluetoothDevice;

    std::function<void(const wchar_t* page)> openSettings;  // e.g. L"ms-settings:sound"
};
