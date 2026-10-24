// Unit tests for Island's pure logic: no window, no hardware.
// Run with:  ctest --preset debug   (after cmake --build --preset debug)
// Each CHECK that fails prints its file and line; the exit code is the
// number of failures, so 0 means everything passed.

#include <windows.h>
#include <Unknwn.h>

#include <winrt/Windows.Foundation.h>

#include <cstdio>

#include "platform/Settings.h"
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
    }
}

int main()
{
    winrt::init_apartment();

    TestClassifier();
    TestSettings();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
