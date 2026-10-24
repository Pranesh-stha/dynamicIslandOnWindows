// Unit tests for Island's pure logic: no window, no hardware.
// Run with:  ctest --preset debug   (after cmake --build --preset debug)
// Each CHECK that fails prints its file and line; the exit code is the
// number of failures, so 0 means everything passed.
//
// "IslandTests.exe --hardware" also checks the real volume, brightness and
// mixer services. It changes the volume, each display's brightness and one
// app's volume by 1 % (too little to notice) and puts them back. It lists
// the battery, Wi-Fi networks and Bluetooth devices but never switches,
// joins or connects anything.

#include <windows.h>
#include <Unknwn.h>
#include <DispatcherQueue.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <Shlwapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "StateMachine.h"
#include "platform/Settings.h"
#include "render/Widgets.h"
#include "render/Glyphs.h"
#include "services/AudioService.h"
#include "services/BluetoothService.h"
#include "services/BrightnessService.h"
#include "services/PowerService.h"
#include "services/WifiService.h"
#include "views/sections/ConnectionsSection.h"
#include "views/sections/SoundSection.h"
#include "services/MediaState.h"
#include "services/Throttle.h"
#include "services/WindowClassifier.h"

namespace
{
    int g_checks = 0;
    int g_failures = 0;

#define CHECK(expression)                                                              \
    do                                                                                 \
    {                                                                                  \
        ++g_checks;                                                                    \
        if (!(expression))                                                             \
        {                                                                              \
            ++g_failures;                                                              \
            std::printf("FAILED  %s(%d): %s\n", __FILE__, __LINE__, #expression);     \
        }                                                                              \
    } while (0)

    constexpr RECT kMonitor{ 0, 0, 2560, 1440 };
    constexpr LONG kNormalWindow = WS_OVERLAPPEDWINDOW | WS_VISIBLE;  // caption + resize border
    constexpr LONG kBorderless = WS_POPUP | WS_VISIBLE;

    WindowFacts Window(RECT rect, LONG style, bool maximized = false)
    {
        WindowFacts facts;
        facts.rect = rect;
        facts.style = style;
        facts.maximized = maximized;
        return facts;
    }

    void TestClassifier()
    {
        // Borderless "windowed fullscreen" game, or exclusive fullscreen.
        CHECK(ClassifyWindow(Window(kMonitor, kBorderless), kMonitor) == FrontKind::Fullscreen);

        // Chrome maximized with an auto-hide taskbar: its invisible resize
        // borders stick out 8 px past every edge, so it covers the monitor,
        // but it's still just maximized.
        CHECK(ClassifyWindow(Window({ -8, -8, 2568, 1448 }, kNormalWindow, true), kMonitor) == FrontKind::Maximized);

        // Maximized with a visible taskbar at the bottom.
        CHECK(ClassifyWindow(Window({ -8, -8, 2568, 1392 }, kNormalWindow, true), kMonitor) == FrontKind::Maximized);

        // Chrome after F11: caption and resize border removed, exactly the
        // monitor's size, and Windows may still call it maximized.
        CHECK(ClassifyWindow(Window(kMonitor, WS_POPUP | WS_VISIBLE | WS_SYSMENU, true), kMonitor) == FrontKind::Fullscreen);

        // A Store app in fullscreen mode keeps its caption but isn't maximized.
        CHECK(ClassifyWindow(Window(kMonitor, kNormalWindow), kMonitor) == FrontKind::Fullscreen);

        // An ordinary window.
        CHECK(ClassifyWindow(Window({ 100, 100, 1200, 900 }, kNormalWindow), kMonitor) == FrontKind::Normal);

        // Maximized, but on the other monitor (to the right).
        CHECK(ClassifyWindow(Window({ 2552, -8, 5128, 1448 }, kNormalWindow, true), kMonitor) == FrontKind::Normal);

        // Fullscreen on the other monitor.
        CHECK(ClassifyWindow(Window({ 2560, 0, 5120, 1440 }, kBorderless), kMonitor) == FrontKind::Normal);

        // Covers the monitor but minimized, cloaked, or a click-through overlay.
        WindowFacts minimized = Window(kMonitor, kBorderless);
        minimized.minimized = true;
        CHECK(ClassifyWindow(minimized, kMonitor) == FrontKind::Normal);

        WindowFacts cloaked = Window(kMonitor, kBorderless);
        cloaked.cloaked = true;
        CHECK(ClassifyWindow(cloaked, kMonitor) == FrontKind::Normal);

        WindowFacts overlay = Window(kMonitor, kBorderless);
        overlay.exStyle = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST;
        CHECK(ClassifyWindow(overlay, kMonitor) == FrontKind::Normal);

        WindowFacts invisible = Window(kMonitor, WS_POPUP);
        invisible.visible = false;
        CHECK(ClassifyWindow(invisible, kMonitor) == FrontKind::Normal);

        // One pixel short of covering is not fullscreen.
        CHECK(ClassifyWindow(Window({ 0, 0, 2560, 1439 }, kBorderless), kMonitor) == FrontKind::Normal);

        // A monitor whose origin isn't (0, 0), e.g. left of the primary.
        constexpr RECT leftMonitor{ -1920, 0, 0, 1080 };
        CHECK(ClassifyWindow(Window(leftMonitor, kBorderless), leftMonitor) == FrontKind::Fullscreen);
        CHECK(ClassifyWindow(Window({ -1928, -8, 8, 1088 }, kNormalWindow, true), leftMonitor) == FrontKind::Maximized);
    }

    void TestSettings()
    {
        Settings settings;
        std::wstring message;

        // Empty file: all defaults.
        CHECK(ParseSettings(L"{}", settings, message));
        CHECK(settings.games.blocklist.empty());
        CHECK(settings.games.showDelayMs == 500);

        // Comments are allowed; exe names are lower-cased; "//" inside a
        // string is not a comment.
        CHECK(ParseSettings(LR"({
                // a comment
                "games": { "blocklist": ["Game.EXE", "http://x"], "showDelayMs": 250 } // trailing
            })", settings, message));
        CHECK(settings.games.blocklist.size() == 2);
        CHECK(settings.games.blocklist.size() == 2 && settings.games.blocklist[0] == L"game.exe");
        CHECK(settings.games.blocklist.size() == 2 && settings.games.blocklist[1] == L"http://x");
        CHECK(settings.games.showDelayMs == 250);

        // Broken JSON: rejected, defaults kept.
        CHECK(!ParseSettings(L"{ \"games\": { ", settings, message));
        CHECK(!message.empty());
        CHECK(settings.games.showDelayMs == 500);

        // A wrongly typed value is ignored (with a message); the rest still loads.
        CHECK(ParseSettings(LR"({ "games": { "showDelayMs": "fast", "blocklist": ["a.exe"] } })", settings, message));
        CHECK(settings.games.showDelayMs == 500);
        CHECK(settings.games.blocklist.size() == 1);
        CHECK(message.find(L"games.showDelayMs") != std::wstring::npos);

        // Out-of-range numbers are clamped.
        CHECK(ParseSettings(LR"({ "games": { "showDelayMs": 999999 } })", settings, message));
        CHECK(settings.games.showDelayMs == 5000);

        // Springs, accent colour and layout sections.
        CHECK(ParseSettings(LR"({ "motion": { "morph": { "dampingRatio": 0.5, "periodMs": 80 } },
                                  "glass": { "accent": "#FF8800" }, "layout": { "idleWidth": 9999 } })", settings, message));
        CHECK(settings.motion.morph.dampingRatio == 0.5f && settings.motion.morph.periodMs == 80.0f);
        CHECK(settings.motion.press.periodMs == MotionSettings{}.press.periodMs);
        CHECK(settings.glass.accent == L"#FF8800");
        CHECK(settings.layout.idleWidth == 600.0f);
    }

    // Records what the state machine asks for, instead of real Windows timers.
    struct FakeHost : StateMachine::Host
    {
        std::map<IslandTimer, int> timers;  // running timers and their delays
        std::vector<std::pair<IslandState, IslandState>> transitions;

        void StartTimer(IslandTimer timer, int milliseconds) override { timers[timer] = milliseconds; }
        void StopTimer(IslandTimer timer) override { timers.erase(timer); }
        void EnterState(IslandState from, IslandState to) override { transitions.push_back({ from, to }); }
        bool Running(IslandTimer timer) const { return timers.count(timer) != 0; }
    };

    // Lets a timer go off, as Windows would, if it's running.
    void Fire(StateMachine& machine, const FakeHost& host, IslandTimer timer)
    {
        if (host.Running(timer))
        {
            machine.TimerElapsed(timer);
        }
    }

    void TestStateMachine()
    {
        {
            // Starts hidden; comes back after the show delay, or at once on request.
            FakeHost host;
            StateMachine machine(host);
            CHECK(machine.State() == IslandState::Hidden);
            machine.FullscreenChanged(false);
            CHECK(host.Running(IslandTimer::Show) && host.timers[IslandTimer::Show] == 500);
            CHECK(machine.State() == IslandState::Hidden);
            Fire(machine, host, IslandTimer::Show);
            CHECK(machine.State() == IslandState::Idle);

            FakeHost otherHost;
            StateMachine immediate(otherHost);
            immediate.FullscreenChanged(false, true);
            CHECK(immediate.State() == IslandState::Idle);
        }
        {
            // Hover opens Peek only after the mouse rests; a quick pass-over doesn't.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.PointerEntered();
            CHECK(host.Running(IslandTimer::HoverIntent) && host.timers[IslandTimer::HoverIntent] == 150);
            machine.PointerLeft();
            CHECK(!host.Running(IslandTimer::HoverIntent));
            CHECK(machine.State() == IslandState::Idle);

            machine.PointerEntered();
            Fire(machine, host, IslandTimer::HoverIntent);
            CHECK(machine.State() == IslandState::Peek);

            // Leaving starts the leave delay; coming back cancels it.
            machine.PointerLeft();
            CHECK(host.Running(IslandTimer::Leave) && host.timers[IslandTimer::Leave] == 300);
            machine.PointerEntered();
            CHECK(!host.Running(IslandTimer::Leave));
            machine.PointerLeft();
            Fire(machine, host, IslandTimer::Leave);
            CHECK(machine.State() == IslandState::Idle);
        }
        {
            // A click opens the control center, which closes with its own, longer delay.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.PointerEntered();
            machine.BackgroundClicked();
            CHECK(machine.State() == IslandState::Expanded);
            CHECK(!host.Running(IslandTimer::HoverIntent));
            machine.PointerLeft();
            CHECK(host.timers[IslandTimer::Leave] == 500);
        }
        {
            // Dragging a slider out of the island doesn't close it; letting go outside does.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.PointerEntered();
            machine.BackgroundClicked();
            machine.PointerCaptured(true);
            machine.PointerLeft();
            CHECK(!host.Running(IslandTimer::Leave));
            machine.PointerCaptured(false);
            CHECK(host.Running(IslandTimer::Leave));
            Fire(machine, host, IslandTimer::Leave);
            CHECK(machine.State() == IslandState::Idle);
        }
        {
            // Fullscreen hides at once from any state and cancels every timer.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.PointerEntered();
            machine.BackgroundClicked();
            machine.PointerLeft();
            machine.FullscreenChanged(true);
            CHECK(machine.State() == IslandState::Hidden);
            CHECK(host.timers.empty());
            CHECK(!machine.PointerInside());

            // Fullscreen again during the show delay: the show timer is cancelled.
            machine.FullscreenChanged(false);
            machine.FullscreenChanged(true);
            CHECK(!host.Running(IslandTimer::Show));
            machine.TimerElapsed(IslandTimer::Show);  // a late timer must not show it
            CHECK(machine.State() == IslandState::Hidden);
        }
        {
            // Dragging a window over the island (Snap Layouts) doesn't open it,
            // and neither does a drag that ends on it, until the mouse leaves.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.WindowDragChanged(true);
            machine.PointerEntered();
            CHECK(!host.Running(IslandTimer::HoverIntent));
            machine.WindowDragChanged(false);
            machine.TimerElapsed(IslandTimer::HoverIntent);
            CHECK(machine.State() == IslandState::Idle);
            machine.PointerLeft();
            machine.PointerEntered();
            CHECK(host.Running(IslandTimer::HoverIntent));

            // A hover timer that fires after the mouse already left does nothing.
            machine.PointerLeft();
            machine.TimerElapsed(IslandTimer::HoverIntent);
            CHECK(machine.State() == IslandState::Idle);
        }
        {
            // Pinned (--pose, "Keep open"): leaving doesn't close; unpinning outside does.
            FakeHost host;
            StateMachine machine(host);
            machine.FullscreenChanged(false, true);
            machine.SetPinned(true);
            machine.ShowDirectly(IslandState::Peek);
            CHECK(machine.State() == IslandState::Peek);
            machine.PointerEntered();
            machine.PointerLeft();
            CHECK(!host.Running(IslandTimer::Leave));
            machine.SetPinned(false);
            CHECK(host.Running(IslandTimer::Leave));

            // Opening from the tray without the mouse on the island closes after a while.
            machine.OpenExpanded(4000);
            CHECK(machine.State() == IslandState::Expanded);
            CHECK(host.timers[IslandTimer::Leave] == 4000);
        }
        {
            // Auto-hide: the island rests out of sight, comes down when the
            // mouse touches the top edge, then behaves as usual.
            FakeHost host;
            StateMachine machine(host);
            machine.SetAutoHide(true);
            machine.FullscreenChanged(false, true);
            CHECK(machine.State() == IslandState::Tucked);
            machine.PointerEntered();
            CHECK(machine.State() == IslandState::Idle);  // the pill
            CHECK(host.Running(IslandTimer::HoverIntent));
            Fire(machine, host, IslandTimer::HoverIntent);
            CHECK(machine.State() == IslandState::Peek);
            machine.BackgroundClicked();
            CHECK(machine.State() == IslandState::Expanded);

            // Leaving the control center goes straight back out of sight.
            machine.PointerLeft();
            Fire(machine, host, IslandTimer::Leave);
            CHECK(machine.State() == IslandState::Tucked);

            // Leaving the pill before Peek: tucked away after the leave delay;
            // coming back in time keeps it out.
            machine.PointerEntered();
            machine.PointerLeft();
            CHECK(machine.State() == IslandState::Idle && host.timers[IslandTimer::Leave] == 300);
            machine.PointerEntered();
            CHECK(!host.Running(IslandTimer::Leave));
            machine.PointerLeft();
            Fire(machine, host, IslandTimer::Leave);
            CHECK(machine.State() == IslandState::Tucked);

            // A window dragged to the top of the screen (Snap Layouts) doesn't bring it out.
            machine.WindowDragChanged(true);
            machine.PointerEntered();
            CHECK(machine.State() == IslandState::Tucked);
            machine.PointerLeft();
            machine.WindowDragChanged(false);

            // Fullscreen hides it; it comes back tucked away.
            machine.FullscreenChanged(true);
            CHECK(machine.State() == IslandState::Hidden);
            machine.FullscreenChanged(false);
            Fire(machine, host, IslandTimer::Show);
            CHECK(machine.State() == IslandState::Tucked);

            // Turning auto-hide off in settings.json shows the pill; on again tucks it.
            machine.SetAutoHide(false);
            CHECK(machine.State() == IslandState::Idle);
            machine.SetAutoHide(true);
            CHECK(machine.State() == IslandState::Tucked);

            // A pose holds the pill; releasing it tucks it away after the delay.
            machine.SetPinned(true);
            machine.ShowDirectly(IslandState::Idle);
            CHECK(!host.Running(IslandTimer::Leave));
            machine.SetPinned(false);
            Fire(machine, host, IslandTimer::Leave);
            CHECK(machine.State() == IslandState::Tucked);
        }
    }

    // A control that writes down every event it gets.
    struct FakeControl : Control
    {
        FakeControl(const Ui& ui, bool keepsPress) : Control(ui), keepsPressOutside(keepsPress) {}
        std::wstring events;
        bool keepsPressOutside;

        void OnHoverChanged(bool hovered) override { events += hovered ? L"H" : L"h"; }
        void OnPressChanged(bool pressed) override { events += pressed ? L"P" : L"p"; }
        void OnClick() override { events += L"C"; }
        void OnPointerUp(bool inside) override { events += inside ? L"U" : L"u"; Control::OnPointerUp(inside); }
        void OnRightClick() override { events += L"R"; Control::OnRightClick(); }
        bool OnWheel(float) override { events += L"W"; return true; }
        bool KeepsPressOutside() const override { return keepsPressOutside; }
    };

    void TestControls(const Ui& ui)
    {
        {
            // Hover, press, release inside = click. Feedback on press, action on release.
            FakeControl button(ui, false);
            button.SetBounds(10, 10, 40, 40);
            ControlHost host;
            host.Add(button);
            host.PointerMove(5, 5);
            CHECK(button.events.empty());
            host.PointerMove(20, 20);
            CHECK(button.events == L"H");
            CHECK(host.PointerDown(20, 20));
            CHECK(button.events == L"HP");
            host.PointerUp(21, 21);
            CHECK(button.events == L"HPpUC");

            // Dragging off a button releases its pressed look and cancels the click.
            button.events.clear();
            host.PointerDown(20, 20);
            host.PointerMove(80, 80);
            host.PointerUp(80, 80);
            CHECK(button.events == L"Pphu");  // pressed, unpressed on leaving, hover gone, released outside: no C

            // Missing every control: the press isn't taken (it's a click on the island itself).
            CHECK(!host.PointerDown(100, 100));
            CHECK(!host.Pressing());

            // Right-click and wheel go to the control under the pointer.
            button.events.clear();
            CHECK(host.RightClick(20, 20));
            CHECK(host.Wheel(20, 20, 1.0f));
            CHECK(!host.Wheel(100, 100, 1.0f));
            CHECK(button.events == L"RW");

            // Leaving the island clears the hover (no highlight left stuck on).
            host.PointerMove(20, 20);
            button.events.clear();
            host.PointerLeave();
            CHECK(button.events == L"h");

            // A disabled control can't be hovered or pressed.
            button.SetEnabled(false);
            CHECK(!host.PointerDown(20, 20));
        }
        {
            // A slider keeps its press when dragged outside, and releasing
            // outside still finishes the drag.
            FakeControl slider(ui, true);
            slider.SetBounds(0, 0, 100, 20);
            ControlHost host;
            host.Add(slider);
            host.PointerMove(50, 10);
            host.PointerDown(50, 10);
            host.PointerMove(300, 10);
            host.PointerUp(300, 10);
            CHECK(slider.events == L"HPphu");  // hover ends too, since the pointer is outside

            // Losing the mouse mid-press (e.g. a system dialog) ends the press without a click.
            slider.events.clear();
            host.PointerMove(50, 10);
            host.PointerDown(50, 10);
            host.CancelPress();
            CHECK(slider.events == L"HPpuh");
        }
        {
            // The real slider: positions map to values, dragging reports
            // non-final values, release reports the final one, and changes
            // from outside are ignored while dragging (no jitter, PLAN.md D9).
            Slider slider(ui);
            slider.SetBounds(0, 0, 216, 24);
            std::vector<std::pair<float, bool>> changes;
            slider.onChange = [&](float value, bool final) { changes.push_back({ value, final }); };
            ControlHost host;
            host.Add(slider);

            host.PointerMove(0, 12);
            host.PointerDown(0, 12);  // the left end = 0
            CHECK(std::abs(slider.Value()) < 0.001f);
            host.PointerMove(108, 12);  // the middle
            CHECK(std::abs(slider.Value() - 0.5f) < 0.001f);
            slider.SetValue(0.9f);  // e.g. the volume keys, while dragging: ignored
            CHECK(std::abs(slider.Value() - 0.5f) < 0.001f);
            host.PointerMove(500, 12);  // far outside: clamps to 1
            CHECK(slider.Value() == 1.0f);
            host.PointerUp(500, 12);
            CHECK(!changes.empty() && changes.back().second && changes.back().first == 1.0f);
            bool anyEarlyFinal = false;
            for (size_t i = 0; i + 1 < changes.size(); ++i)
            {
                anyEarlyFinal |= changes[i].second;
            }
            CHECK(!anyEarlyFinal);

            // After the drag, outside changes apply again; the wheel nudges and is final.
            slider.SetValue(0.25f, false);
            CHECK(slider.Value() == 0.25f);
            changes.clear();
            host.Wheel(108, 12, 1.0f);
            CHECK(changes.size() == 1 && changes[0].second && std::abs(changes[0].first - 0.29f) < 0.001f);
        }
    }

    void TestMedia()
    {
        // Picking which app's media to show (PLAN.md E1).
        CHECK(PickSession({}, -1, -1) == -1);
        // Playing beats paused, even if the paused one changed more recently or is shown.
        CHECK(PickSession({ { false, 900, true }, { true, 100, false } }, -1, 0) == 1);
        // Two playing: the most recently active.
        CHECK(PickSession({ { true, 100, false }, { true, 300, false }, { true, 200, false } }, -1, -1) == 1);
        // Nothing playing: keep the one shown (paused apps don't take turns as
        // Windows reshuffles them), else the one Windows shows, else the most recent.
        CHECK(PickSession({ { false, 900, true }, { false, 100, false } }, -1, 1) == 1);
        CHECK(PickSession({ { false, 900, false }, { false, 100, true } }, -1, -1) == 1);
        CHECK(PickSession({ { false, 100, false }, { false, 900, false } }, -1, -1) == 1);
        // The shown session went away (a browser replacing it): stay with that app.
        CHECK(PickSession({ { false, 900, true, false }, { false, 100, false, true } }, -1, -1) == 1);
        // A manual choice wins while it exists.
        CHECK(PickSession({ { true, 900, false }, { false, 100, false } }, 1, 0) == 1);
        CHECK(PickSession({ { true, 900, false } }, 5, -1) == 0);

        // Friendly names for the ids Windows gives players.
        CHECK(FriendlyAppName(L"Spotify.exe") == L"Spotify");
        CHECK(FriendlyAppName(L"chrome") == L"Chrome");
        CHECK(FriendlyAppName(L"MSEdge") == L"Edge");
        CHECK(FriendlyAppName(L"Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == L"Media Player");
        CHECK(FriendlyAppName(L"308046B0AF4A39CB") == L"Firefox");
        CHECK(FriendlyAppName(L"C:\\Games\\coolplayer.exe") == L"Coolplayer");
        CHECK(FriendlyAppName(L"Contoso.MusicApp_abc123!App") == L"MusicApp");
        CHECK(FriendlyAppName(L"") == L"Media");

        // Where the song is now: counted on from when the app last said, only while playing.
        using namespace std::chrono_literals;
        MediaState media;
        media.hasTimeline = true;
        media.durationSeconds = 200.0;
        media.positionSeconds = 50.0;
        const auto then = winrt::clock::now();
        media.positionMeasuredAt = then;
        media.playing = true;
        CHECK(std::abs(media.PositionAt(then + 10s) - 60.0) < 0.001);
        media.rate = 2.0;
        CHECK(std::abs(media.PositionAt(then + 10s) - 70.0) < 0.001);
        CHECK(media.PositionAt(then + 1000s) == 200.0);  // never past the end
        media.playing = false;
        CHECK(media.PositionAt(then + 10s) == 50.0);  // paused: it stays put
        media.playing = true;
        media.positionMeasuredAt = {};  // the app never said when
        CHECK(media.PositionAt(then) == 50.0);
    }

    void TestQuickControls()
    {
        // Wi-Fi: one row per name, the strongest access point wins; hidden
        // networks dropped; connected first, then saved, then by signal.
        const auto networks = GroupNetworks(
            { { L"Home", 2, true }, { L"Home", 4, true }, { L"", 4, false }, { L"Cafe", 3, false }, { L"Neighbour", 4, true }, { L"Work", 1, true } },
            L"Work", { L"Home", L"Work" });
        CHECK(networks.size() == 4);
        CHECK(networks.size() == 4 && networks[0].ssid == L"Work" && networks[0].connected);
        CHECK(networks.size() == 4 && networks[1].ssid == L"Home" && networks[1].bars == 4 && networks[1].saved);
        CHECK(networks.size() == 4 && networks[2].ssid == L"Neighbour" && !networks[2].saved);
        CHECK(networks.size() == 4 && networks[3].ssid == L"Cafe" && !networks[3].secured);
        // The connected network missing from a scan still shows, at the top.
        const auto missing = GroupNetworks({ { L"Cafe", 3, false } }, L"Home", { L"Home" });
        CHECK(missing.size() == 2 && missing[0].ssid == L"Home" && missing[0].connected);

        // Mixer: an app's sessions merge; playing apps first, Windows' sounds last.
        const auto apps = GroupSessions({
            { L"system", L"System sounds", 1.0f, false, false, true },
            { L"chrome.exe", L"Chrome", 0.5f, false, false, false },
            { L"spotify.exe", L"Spotify", 0.8f, false, true, false },
            { L"chrome.exe", L"Chrome", 0.7f, true, true, false },
        });
        CHECK(apps.size() == 3);
        CHECK(apps.size() == 3 && apps[0].key == L"chrome.exe" && apps[0].active && !apps[0].muted && apps[0].volume == 0.7f);
        CHECK(apps.size() == 3 && apps[1].key == L"spotify.exe");
        CHECK(apps.size() == 3 && apps[2].system);
        CHECK(FriendlyExeName(L"VALORANT-Win64-Shipping.exe") == L"VALORANT");
        CHECK(FriendlyExeName(L"chrome.exe") == L"Chrome");
        CHECK(FriendlyExeName(L"Discord.exe") == L"Discord");

        // Mixer rows keep their app, so sliders never swap under the mouse.
        std::array<std::wstring, 3> rows{};
        AssignMixerRows(rows, { L"a", L"b" });
        CHECK(rows[0] == L"a" && rows[1] == L"b" && rows[2].empty());
        AssignMixerRows(rows, { L"c", L"b", L"a" });  // a new app sorted first still goes to a free row
        CHECK(rows[0] == L"a" && rows[1] == L"b" && rows[2] == L"c");
        AssignMixerRows(rows, { L"b", L"d", L"e" });  // a and c left: d fills a free row; no room for e
        CHECK(rows[0] == L"d" && rows[1] == L"b" && rows[2].empty() == false && rows[2] == L"e");

        // Bluetooth: the device kind from its "class of device".
        CHECK(KindFromClassOfDevice(4, 6) == DeviceKind::Audio);         // headphones
        CHECK(KindFromClassOfDevice(5, 0x20) == DeviceKind::Mouse);      // pointing device
        CHECK(KindFromClassOfDevice(5, 0x10) == DeviceKind::Keyboard);
        CHECK(KindFromClassOfDevice(5, 0x02) == DeviceKind::Gamepad);
        CHECK(KindFromClassOfDevice(2, 3) == DeviceKind::Phone);
        CHECK(KindFromClassOfDevice(0, 0) == DeviceKind::Other);
        std::vector<BluetoothDevice> devices{ { L"1", L"zeta" }, { L"2", L"Alpha" }, { L"3", L"Buds", DeviceKind::Audio, true, true },
                                              { L"4", L"Speaker", DeviceKind::Audio, false, true } };
        SortDevices(devices);
        CHECK(devices[0].name == L"Buds" && devices[1].name == L"Speaker" && devices[2].name == L"Alpha" && devices[3].name == L"zeta");

        // Long lists page: 3 lines + a "more" row per page of 4, wrapping around.
        auto page = PageLines(3, 4, 0);
        CHECK(!page.more && page.first == 0 && page.count == 3);
        page = PageLines(7, 4, 0);
        CHECK(page.more && page.first == 0 && page.count == 3);
        page = PageLines(7, 4, 2);
        CHECK(page.more && page.first == 6 && page.count == 1);
        page = PageLines(7, 4, 3);  // past the last page: back to the first
        CHECK(page.page == 0 && page.first == 0);

        // Battery text and icon.
        PowerState power;
        power.hasBattery = true;
        power.percent = 84;
        power.minutesLeft = 130;
        CHECK(PowerDescription(power) == L"2 h 10 min left");
        CHECK(BatteryGlyph(power) == static_cast<wchar_t>(Glyph::Battery0 + 8));
        power.charging = power.pluggedIn = true;
        power.minutesToFull = 45;
        CHECK(PowerDescription(power) == L"Charging · 45 min to full");
        CHECK(BatteryGlyph(power) == static_cast<wchar_t>(Glyph::BatteryCharging0 + 8));
        power.charging = false;
        power.percent = 100;
        CHECK(PowerDescription(power) == L"Fully charged");
        PowerState desktop;
        CHECK(PowerDescription(desktop) == L"Plugged in" && BatteryGlyph(desktop) == Glyph::Plug);
    }

    void TestThrottle()
    {
        // Brightness writes to a slow monitor (PLAN.md E7, E9).
        Throttle throttle(250);
        int64_t next = 0;
        CHECK(!throttle.Take(0, next) && next == Throttle::kNever);  // nothing wanted

        throttle.Want(40);
        CHECK(throttle.Take(1000, next) == 40);  // the first write goes out at once

        // A drag: values arrive every few ms; only the newest is sent, at most every 250 ms.
        throttle.Want(41);
        throttle.Want(45);
        CHECK(!throttle.Take(1100, next) && next == 1250);
        throttle.Want(52);
        CHECK(throttle.Take(1250, next) == 52);  // 41 and 45 were dropped

        // The final value is sent even if it comes right after a write.
        throttle.Want(60);
        CHECK(!throttle.Take(1260, next) && next == 1500);
        CHECK(throttle.Take(1500, next) == 60);
        CHECK(!throttle.Take(5000, next) && next == Throttle::kNever);

        // Wanting what the monitor already has sends nothing.
        throttle.Want(60);
        CHECK(!throttle.Take(6000, next) && next == Throttle::kNever);
        throttle.Known(30);  // changed with the monitor's own buttons
        throttle.Want(60);
        CHECK(throttle.Take(6000, next) == 60);
    }

    void TestArtworkDecode()
    {
        // A 400x300 picture (like a video thumbnail): a red strip down the
        // left 40 px, blue everywhere else, as an uncompressed BMP file.
        constexpr int width = 400, height = 300;
        std::vector<uint8_t> file(54 + width * height * 3);
        BITMAPFILEHEADER fileHeader{ 0x4D42, static_cast<DWORD>(file.size()), 0, 0, 54 };
        BITMAPINFOHEADER info{ sizeof(BITMAPINFOHEADER), width, height, 1, 24, BI_RGB };
        std::memcpy(file.data(), &fileHeader, 14);
        std::memcpy(file.data() + 14, &info, 40);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                uint8_t* pixel = file.data() + 54 + (y * width + x) * 3;  // blue, green, red
                pixel[0] = x < 40 ? 0 : 255;
                pixel[1] = 0;
                pixel[2] = x < 40 ? 255 : 0;
            }
        }
        winrt::com_ptr<IStream> stream;
        stream.attach(SHCreateMemStream(file.data(), static_cast<UINT>(file.size())));
        const auto art = DecodeArtwork(stream.get(), 192);
        UINT artWidth = 0, artHeight = 0;
        art->GetSize(&artWidth, &artHeight);
        CHECK(artWidth == 192 && artHeight == 192);

        // Scaled to 256x192, then the middle 192 columns: the red strip (now
        // 26 px wide) is cut off, so the left edge is blue.
        std::vector<uint8_t> pixels(192 * 192 * 4);
        CHECK(SUCCEEDED(art->CopyPixels(nullptr, 192 * 4, static_cast<UINT>(pixels.size()), pixels.data())));
        auto at = [&](int x, int y) { return &pixels[(y * 192 + x) * 4]; };  // blue, green, red, alpha
        CHECK(at(1, 96)[0] > 200 && at(1, 96)[2] < 60);
        CHECK(at(190, 96)[0] > 200 && at(190, 96)[3] == 255);

        // A broken file is an error, never a crash.
        std::vector<uint8_t> junk(100, 7);
        winrt::com_ptr<IStream> broken;
        broken.attach(SHCreateMemStream(junk.data(), static_cast<UINT>(junk.size())));
        bool threw = false;
        try
        {
            DecodeArtwork(broken.get(), 192);
        }
        catch (const winrt::hresult_error&)
        {
            threw = true;
        }
        CHECK(threw);
    }

    // Runs this thread's message loop (where the services deliver their
    // results) until `done` or the timeout.
    bool PumpUntil(const std::function<bool()>& done, int timeoutMs)
    {
        const uint64_t end = GetTickCount64() + timeoutMs;
        while (!done() && GetTickCount64() < end)
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                DispatchMessageW(&message);
            }
            Sleep(5);
        }
        return done();
    }

    void TestHardware()
    {
        // Volume: a change made outside Island must reach it (volume keys, Quick Settings).
        {
            AudioState last;
            int updates = 0;
            AudioService audio;
            audio.Start([&](const AudioState& state) { last = state; ++updates; });
            CHECK(PumpUntil([&] { return updates > 0; }, 2000) && last.available);
            std::printf("  audio: %ls at %.0f %%\n", last.device.c_str(), last.volume * 100.0f);

            winrt::com_ptr<IMMDeviceEnumerator> devices;
            winrt::com_ptr<IMMDevice> device;
            winrt::com_ptr<IAudioEndpointVolume> endpoint;
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices.put()));
            devices->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put());
            device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, endpoint.put_void());
            float before = 0.0f;
            endpoint->GetMasterVolumeLevelScalar(&before);
            const float nudged = before > 0.5f ? before - 0.01f : before + 0.01f;
            endpoint->SetMasterVolumeLevelScalar(nudged, nullptr);  // "someone else" changes it
            CHECK(PumpUntil([&] { return std::abs(last.volume - nudged) < 0.002f; }, 2000));
            endpoint->SetMasterVolumeLevelScalar(before, nullptr);
            PumpUntil([&] { return std::abs(last.volume - before) < 0.002f; }, 2000);
            CHECK(std::abs(last.volume - before) < 0.002f);
        }

        // Brightness: every display is found; a write through the service
        // reaches the hardware (read back after a refresh), then it's restored.
        {
            BrightnessState last;
            int updates = 0;
            BrightnessService brightness;
            brightness.Start([&](const BrightnessState& state) { last = state; ++updates; });
            CHECK(PumpUntil([&] { return updates > 0; }, 5000) && !last.displays.empty());
            for (const DisplayBrightness& display : BrightnessState(last).displays)
            {
                std::printf("  brightness: %ls%ls: %s, %d %%\n", display.name.c_str(), display.primary ? L" (primary)" : L"",
                    display.supported ? "controllable" : "NOT controllable", display.value);
                if (!display.supported)
                {
                    continue;
                }
                const int original = display.value;
                const int target = original > 50 ? original - 1 : original + 1;
                brightness.Set(display.id, target / 100.0f);
                Sleep(2500);  // past the "keep our own value" window, so the refresh really reads the hardware
                updates = 0;
                brightness.Refresh();
                PumpUntil([&] { return updates > 0; }, 5000);
                int readBack = -1;
                for (const DisplayBrightness& now : last.displays)
                {
                    if (now.id == display.id)
                    {
                        readBack = now.value;
                    }
                }
                std::printf("    set %d %%, read back %d %%\n", target, readBack);
                CHECK(readBack == target);
                brightness.Set(display.id, original / 100.0f);  // put it back
                Sleep(600);
            }
        }

        // Mixer: the apps playing sound are listed; an app's volume set through
        // the service reaches Windows (a fresh service reads it back), then it's
        // restored. Muted apps are left alone (raising the volume would unmute them).
        {
            AudioState last;
            AudioService audio;
            audio.Start([&](const AudioState& state) { last = state; });
            PumpUntil([&] { return last.available; }, 2000);
            std::printf("  mixer: %zu apps\n", last.apps.size());
            const AppVolume* app = nullptr;
            for (const AppVolume& entry : last.apps)
            {
                std::printf("    %ls: %.0f %%%s%s\n", entry.name.c_str(), entry.volume * 100.0f, entry.muted ? ", muted" : "",
                    entry.active ? ", playing" : "");
                if (!app && !entry.system && !entry.muted)
                {
                    app = &entry;
                }
            }
            if (app)
            {
                const std::wstring key = app->key;
                const float before = app->volume;
                const float nudged = before > 0.5f ? before - 0.01f : before + 0.01f;
                audio.SetAppVolume(key, nudged);
                auto readBack = [&]
                {
                    AudioState fresh;
                    AudioService reader;
                    reader.Start([&](const AudioState& state) { fresh = state; });
                    PumpUntil([&] { return fresh.available; }, 2000);
                    for (const AppVolume& entry : fresh.apps)
                    {
                        if (entry.key == key)
                        {
                            return entry.volume;
                        }
                    }
                    return -1.0f;
                };
                const float got = readBack();
                std::printf("    %ls set to %.0f %%, read back %.0f %%\n", app->name.c_str(), nudged * 100.0f, got * 100.0f);
                CHECK(std::abs(got - nudged) < 0.002f);
                audio.SetAppVolume(key, before);  // put it back
                CHECK(std::abs(readBack() - before) < 0.002f);
            }
        }

        // Battery, Wi-Fi networks and Bluetooth devices: read only. Nothing is
        // switched, joined, connected or paired.
        {
            PowerService power;
            power.Start([](const PowerState&) {});
            std::printf("  power: %s%d %%, %ls\n", power.Current().hasBattery ? "battery " : "no battery, ", power.Current().percent,
                PowerDescription(power.Current()).c_str());

            WifiState wifi;
            WifiService wifiService;
            wifiService.Start([&](const WifiState& state) { wifi = state; });
            PumpUntil([&] { return wifi.available; }, 5000);
            wifiService.Scan();
            PumpUntil([&] { return wifi.scanning; }, 1000);
            PumpUntil([&] { return !wifi.scanning; }, 10000);
            const auto connected = std::count_if(wifi.networks.begin(), wifi.networks.end(), [](const WifiNetwork& n) { return n.connected; });
            std::printf("  wifi: %s, %zu networks after a scan, %s\n", wifi.available ? "adapter found" : "NO ADAPTER", wifi.networks.size(),
                connected ? "connected to one" : "not connected");
            CHECK(wifi.available && !wifi.networks.empty());

            BluetoothState bluetooth;
            BluetoothService bluetoothService;
            bluetoothService.Start([&](const BluetoothState& state) { bluetooth = state; });
            PumpUntil([&] { return !bluetooth.paired.empty(); }, 5000);
            PumpUntil([] { return false; }, 1000);  // the audio-device lookup
            std::printf("  bluetooth: %zu paired devices\n", bluetooth.paired.size());
            for (const BluetoothDevice& device : bluetooth.paired)
            {
                std::printf("    %ls: %ls%s\n", device.name.c_str(), device.detail.c_str(), device.canConnect ? " (Island can connect it)" : "");
            }
        }
    }

    // The compositor needs a dispatcher queue on its thread, as in the app.
    winrt::Windows::System::DispatcherQueueController CreateDispatcherQueue()
    {
        DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA };
        winrt::Windows::System::DispatcherQueueController controller{ nullptr };
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));
        return controller;
    }
}

int main(int argc, char** argv)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    const bool hardware = argc > 1 && std::strcmp(argv[1], "--hardware") == 0;

    TestClassifier();
    TestSettings();
    TestStateMachine();
    TestMedia();
    TestArtworkDecode();
    TestThrottle();
    TestQuickControls();

    // Controls need a real compositor (no window: nothing is shown).
    const auto queue = CreateDispatcherQueue();
    const MotionSettings motion;
    const LayoutSettings layout;
    Ui ui;
    ui.compositor = winrt::Windows::UI::Composition::Compositor();
    ui.motion = &motion;
    ui.layout = &layout;
    ui.scale = 1.25f;
    TestControls(ui);

    if (hardware)
    {
        std::printf("hardware checks (volume, brightness and one app's volume move by 1 %% and back; Wi-Fi and Bluetooth are only read):\n");
        TestHardware();
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
