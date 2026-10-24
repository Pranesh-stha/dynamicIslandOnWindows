#pragma once

#include <windows.h>

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

    bool Start(HINSTANCE instance);

private:
    std::vector<TrayItem> BuildTrayMenu();
    void ApplySettings();

    // Members are destroyed in reverse order: services first, the window last.
    SettingsFile m_settings;
    IslandWindow m_island;
    Tray m_tray;
    FullscreenWatcher m_fullscreen;
};
