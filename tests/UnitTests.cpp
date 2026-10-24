// Unit tests for Island's pure logic: no window, no hardware.
// Run with:  ctest --preset debug   (after cmake --build --preset debug)
// Each CHECK that fails prints its file and line; the exit code is the
// number of failures, so 0 means everything passed.

#include <windows.h>
#include <Unknwn.h>
#include <DispatcherQueue.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "StateMachine.h"
#include "platform/Settings.h"
#include "render/Widgets.h"
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
        void OnRightClick() override { events += L"R"; }
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
            const float thumb = ui.EvenPx(16.0f);
            slider.SetBounds(0, 0, 216, 24);
            std::vector<std::pair<float, bool>> changes;
            slider.onChange = [&](float value, bool final) { changes.push_back({ value, final }); };
            ControlHost host;
            host.Add(slider);

            host.PointerMove(thumb / 2, 12);
            host.PointerDown(thumb / 2, 12);  // the thumb's leftmost position = 0
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

int main()
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    TestClassifier();
    TestSettings();
    TestStateMachine();

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

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
