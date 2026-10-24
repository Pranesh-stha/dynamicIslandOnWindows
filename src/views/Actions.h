#pragma once

#include <functional>
#include <string>

// What the views can ask the rest of Island to do. App fills these in, so
// the views never talk to services directly.
struct IslandActions
{
    // Now playing
    std::function<void()> playPause;
    std::function<void()> next;
    std::function<void()> previous;
    std::function<void()> nextMediaSession;  // when several apps play media

    // Quick controls
    std::function<void(float volume)> setVolume;  // 0..1
    std::function<void()> toggleMute;
    std::function<void(const std::wstring& display, float value)> setBrightness;  // 0..1
    std::function<void(bool on)> setWifi;
    std::function<void(bool on)> setBluetooth;
    std::function<void(const wchar_t* page)> openSettings;  // e.g. L"ms-settings:sound"
};
