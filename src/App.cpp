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

namespace
{
    IslandPose PoseFromCommandLine(std::wstring_view commandLine)
    {
        const size_t at = commandLine.find(L"--pose ");
        if (at == std::wstring_view::npos)
        {
            return IslandPose::None;
        }
        const std::wstring_view name = commandLine.substr(at + 7);
        if (name.starts_with(L"idle")) return IslandPose::Idle;
        if (name.starts_with(L"peek")) return IslandPose::Peek;
        if (name.starts_with(L"expanded")) return IslandPose::Expanded;
        if (name.starts_with(L"demo")) return IslandPose::Demo;
        return IslandPose::None;
    }
}

bool App::Start(HINSTANCE instance, std::wstring_view commandLine)
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

    // Now playing: the service reports changes, the island's buttons send commands.
    IslandActions& actions = m_island.Actions();
    actions.playPause = [this] { m_media.PlayPause(); };
    actions.next = [this] { m_media.Next(); };
    actions.previous = [this] { m_media.Previous(); };
    actions.nextMediaSession = [this] { m_media.ShowNextSession(); };
    m_media.Start([this](const MediaState& media) { m_island.SetMedia(media); });
    m_island.OnStateChanged([this](IslandState state)
    {
        // Windows' media events occasionally stop; re-read when the island opens.
        if (state == IslandState::Peek || state == IslandState::Expanded)
        {
            m_media.Refresh();
        }
    });

    // If a game is already fullscreen when Island starts, stay hidden.
    m_island.SetSlim(m_fullscreen.Current().slim);
    m_island.SetFullscreenHidden(m_fullscreen.Current().hide, /*immediately*/ true);
    if (const IslandPose pose = PoseFromCommandLine(commandLine); pose != IslandPose::None)
    {
        m_island.Pose(pose);
    }
    return true;
}

void App::ApplySettings()
{
    const Settings& settings = m_settings.Current();
    m_island.ApplySettings(settings);
    m_fullscreen.SetBlocklist(settings.games.blocklist);
}

std::vector<TrayItem> App::BuildTrayMenu()
{
    std::vector<TrayItem> items;
    items.push_back({ L"Island", nullptr });  // no action: shown grayed out, as a title
    items.push_back(TrayItem::Separator());
    items.push_back({ L"Control kit demo", [this] { m_island.OpenControlKitDemo(); } });
    TrayItem keepOpen{ L"Keep open (for tuning)", [this]
    {
        m_island.Pose(m_island.Posed() ? IslandPose::None : IslandPose::Expanded);
    } };
    keepOpen.checked = m_island.Posed();
    items.push_back(keepOpen);
    if (!m_settings.Path().empty())
    {
        items.push_back({ L"Open settings.json", [this] { OpenWithDefaultApp(m_settings.Path()); } });
    }
    items.push_back({ L"Open log", [] { OpenWithDefaultApp(Log::Path()); } });
    items.push_back(TrayItem::Separator());
    items.push_back({ L"Quit Island", [] { PostQuitMessage(0); } });
    return items;
}
