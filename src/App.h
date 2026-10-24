#pragma once

#include <windows.h>

#include <string_view>

#include "IslandWindow.h"
#include "platform/Settings.h"
#include "platform/Tray.h"
#include "services/AudioService.h"
#include "services/BluetoothService.h"
#include "services/DisplayService.h"
#include "services/NightLightService.h"
#include "services/PowerService.h"
#include "services/WifiService.h"
#include "services/BrightnessService.h"
#include "services/FullscreenWatcher.h"
#include "services/KeepAwake.h"
#include "services/SystemSleep.h"
#include "services/TimerService.h"
#include "services/MediaService.h"
#include "services/RadioService.h"

// Owns every part of Island and connects them: services report what's going
// on (fullscreen apps, ...) and the island window reacts.
class App
{
public:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // commandLine may hold "--pose idle|peek|expanded|demo" (see AppPoseMessage).
    bool Start(HINSTANCE instance, std::wstring_view commandLine);

private:
    std::vector<TrayItem> BuildTrayMenu();
    void ApplySettings();

    // Members are destroyed in reverse order: services first, the window last.
    SettingsFile m_settings;
    IslandWindow m_island;
    Tray m_tray;
    FullscreenWatcher m_fullscreen;
    MediaService m_media;
    AudioService m_audio;
    BrightnessService m_brightness;
    RadioService m_radios;
    WifiService m_wifi;
    BluetoothService m_bluetooth;
    PowerService m_power;
    NightLightService m_nightLight;
    DisplayService m_displays;
    KeepAwake m_keepAwake;
    TimerService m_timer;
    bool m_powerKnown = false;
    bool m_wasPluggedIn = false;
};
