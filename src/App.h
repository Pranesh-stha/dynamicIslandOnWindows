#pragma once

#include <windows.h>

#include <string_view>

#include "IslandWindow.h"
#include "platform/Settings.h"
#include "platform/Tray.h"
#include "services/FullscreenWatcher.h"

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
};
