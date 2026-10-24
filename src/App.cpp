#include "App.h"

#include <shellapi.h>

#include <chrono>

#include "platform/Log.h"
#include "platform/Startup.h"

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

    // Tests only: clear glass from one snapshot, so test screenshots can see the island.
    const bool glassSnapshot = commandLine.find(L"--glass-snapshot") != std::wstring_view::npos;
    if (!m_island.Create(instance, glassSnapshot) ||
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

    // Quick controls.
    actions.setVolume = [this](float volume) { m_audio.SetVolume(volume); };
    actions.toggleMute = [this] { m_audio.ToggleMute(); };
    actions.setAppVolume = [this](const std::wstring& app, float volume) { m_audio.SetAppVolume(app, volume); };
    actions.toggleAppMute = [this](const std::wstring& app) { m_audio.ToggleAppMute(app); };
    actions.toggleMicMute = [this] { m_audio.ToggleMicMute(); };
    actions.setNightLight = [this](bool on)
    {
        if (!m_nightLight.Set(on))
        {
            OpenWithDefaultApp(L"ms-settings:nightlight");
        }
    };
    actions.setProjection = [this](Projection projection) { m_displays.SetProjection(projection); };
    actions.setKeepAwake = [this](bool on)
    {
        m_keepAwake.Set(on);
        m_island.SetKeepAwake(m_keepAwake.On());
    };
    actions.startTimer = [this](int minutes) { m_timer.StartCountdown(minutes); };
    actions.startStopwatch = [this] { m_timer.StartStopwatch(); };
    actions.pauseResumeTimer = [this] { m_timer.PauseResume(); };
    actions.addTimerMinute = [this] { m_timer.AddMinute(); };
    actions.stopTimer = [this] { m_timer.Stop(); };
    actions.sleepNow = []
    {
        // A moment's pause, so the button's press shows and your click is
        // over before the screen goes dark.
        auto timer = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.IsRepeating(false);
        timer.Interval(std::chrono::milliseconds(450));
        timer.Tick([timer](auto&&, auto&&) mutable
        {
            timer.Stop();
            SystemSleep::Now();
        });
        timer.Start();
    };
    actions.setBrightness = [this](const std::wstring& display, float value) { m_brightness.Set(display, value); };
    actions.setWifi = [this](bool on) { m_radios.SetWifi(on); };
    actions.setBluetooth = [this](bool on) { m_radios.SetBluetooth(on); };
    actions.scanWifi = [this] { m_wifi.Scan(); };
    actions.connectWifi = [this](const std::wstring& ssid) { m_wifi.Connect(ssid); };
    actions.toggleBluetoothDevice = [this](const std::wstring& device) { m_bluetooth.Toggle(device); };
    actions.startBluetoothSearch = [this] { m_bluetooth.StartSearch(); };
    actions.stopBluetoothSearch = [this] { m_bluetooth.StopSearch(); };
    actions.pairBluetoothDevice = [this](const std::wstring& device) { m_bluetooth.Pair(device); };
    actions.openSettings = [](const wchar_t* page) { OpenWithDefaultApp(page); };
    m_audio.Start([this](const AudioState& audio) { m_island.SetAudio(audio); });
    m_brightness.Start([this](const BrightnessState& brightness) { m_island.SetBrightness(brightness); });
    m_radios.Start([this](const RadiosState& radios) { m_island.SetRadios(radios); });
    m_wifi.Start([this](const WifiState& wifi) { m_island.SetWifi(wifi); });
    m_bluetooth.Start([this](const BluetoothState& bluetooth) { m_island.SetBluetooth(bluetooth); });
    m_power.Start([this](const PowerState& power)
    {
        // The charger was just plugged in (not at start, and only with a battery).
        if (m_powerKnown && power.hasBattery && power.pluggedIn && !m_wasPluggedIn)
        {
            m_island.ShowChargingMoment(power.percent);
        }
        m_powerKnown = true;
        m_wasPluggedIn = power.pluggedIn;
        m_island.SetPower(power);
    });
    m_timer.Start([this](const TimerState& timer) { m_island.SetTimer(timer); });
    m_nightLight.Start([this](const NightLightState& nightLight) { m_island.SetNightLight(nightLight); });
    m_displays.Start([this](const DisplaysState& displays) { m_island.SetDisplays(displays); });
    m_island.OnDisplaysChanged([this]
    {
        m_brightness.Refresh();
        m_displays.Refresh();
    });
    m_island.OnResumed([this]
    {
        m_fullscreen.Reevaluate();
        m_media.Refresh();
        m_brightness.Refresh();
        m_displays.Refresh();
        m_wifi.Scan();
    });

    m_island.OnStateChanged([this](IslandState state)
    {
        // Windows' media events occasionally stop, and monitors can't tell us
        // when their brightness changes: re-read when the island opens.
        if (state == IslandState::Peek || state == IslandState::Expanded)
        {
            m_media.Refresh();
        }
        if (state == IslandState::Expanded)
        {
            m_brightness.Refresh();
        }
    });

    // If a game is already fullscreen when Island starts, stay hidden.
    m_island.SetSlim(m_fullscreen.Current().slim);
    m_island.SetFullscreenHidden(m_fullscreen.Current().hide, /*immediately*/ true);
    if (const IslandPose pose = PoseFromCommandLine(commandLine); pose != IslandPose::None)
    {
        m_island.Pose(pose);
    }

    // Tests only (hands-free checks): start a timer or the stopwatch, or play
    // the charging moment, as if you had clicked or plugged in the charger.
    if (const size_t at = commandLine.find(L"--timer "); at != std::wstring_view::npos)
    {
        m_timer.StartCountdown(_wtoi(std::wstring(commandLine.substr(at + 8)).c_str()));
    }
    if (commandLine.find(L"--stopwatch") != std::wstring_view::npos)
    {
        m_timer.StartStopwatch();
    }
    if (commandLine.find(L"--charging-moment") != std::wstring_view::npos)
    {
        auto later = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        later.IsRepeating(false);
        later.Interval(std::chrono::milliseconds(1500));  // once the island is up
        later.Tick([this, later](auto&&, auto&&) mutable
        {
            later.Stop();
            m_island.ShowChargingMoment(m_power.Current().percent);
        });
        later.Start();
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
    TrayItem startup{ L"Start with Windows", [] { Startup::SetEnabled(!Startup::Enabled()); } };
    startup.checked = Startup::Enabled();
    items.push_back(startup);
    items.push_back({ L"Quit Island", [] { PostQuitMessage(0); } });
    return items;
}
