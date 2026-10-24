#include "App.h"

#include <shellapi.h>

#include "platform/Log.h"

namespace
{
    void OpenWithDefaultApp(const std::wstring& path)
    {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

bool App::Start(HINSTANCE instance)
{
    m_settings.Load();

    if (!m_island.Create(instance) ||
        !m_tray.Create(instance, [this] { return BuildTrayMenu(); }))
    {
        return false;
    }

    const bool watching = m_fullscreen.Start(instance,
        [this](FullscreenWatcher::State state)
        {
            m_island.SetSlim(state.slim);
            m_island.SetFullscreenHidden(state.hide);
        },
        [this](bool active) { m_island.SetWindowDragActive(active); });
    if (!watching)
    {
        Log::Info(L"fullscreen watcher failed to start");
    }

    ApplySettings();
    m_settings.Watch([this] { ApplySettings(); });

    // If a game is already fullscreen when Island starts, stay hidden.
    m_island.SetSlim(m_fullscreen.Current().slim);
    m_island.SetFullscreenHidden(m_fullscreen.Current().hide, /*immediately*/ true);
    return true;
}

void App::ApplySettings()
{
    const Settings& settings = m_settings.Current();
    m_island.SetShowDelay(settings.games.showDelayMs);
    m_fullscreen.SetBlocklist(settings.games.blocklist);
}

std::vector<TrayItem> App::BuildTrayMenu()
{
    std::vector<TrayItem> items;
    items.push_back({ L"Island", nullptr });  // no action: shown grayed out, as a title
    items.push_back(TrayItem::Separator());
    if (!m_settings.Path().empty())
    {
        items.push_back({ L"Open settings.json", [this] { OpenWithDefaultApp(m_settings.Path()); } });
    }
    items.push_back({ L"Open log", [] { OpenWithDefaultApp(Log::Path()); } });
    items.push_back(TrayItem::Separator());
    items.push_back({ L"Quit Island", [] { PostQuitMessage(0); } });
    return items;
}
