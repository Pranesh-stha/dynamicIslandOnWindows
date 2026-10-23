#include "IslandWindow.h"

#include <dwmapi.h>
#include <ShellScalingApi.h>

#include <cmath>

#include "AppMessages.h"

namespace
{
    // Pill size in DIPs (device-independent pixels: 1 DIP = 1 pixel at 100 %
    // scaling, 1.25 pixels at 125 %, and so on). Phase 3 moves these into settings.json.
    constexpr float kPillWidth = 200.0f;
    constexpr float kPillHeight = 34.0f;
    constexpr float kTopGap = 6.0f;

    // Display changes arrive in bursts (docking, closing the lid), so we wait
    // until they settle before repositioning.
    constexpr UINT_PTR kPlacementTimer = 1;
    constexpr UINT kPlacementDelayMs = 250;

    template <typename T>
    void SetDwmAttribute(HWND hwnd, DWMWINDOWATTRIBUTE attribute, T value)
    {
        DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
    }
}

IslandWindow::~IslandWindow()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
    }
}

bool IslandWindow::Create(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // WS_EX_TOPMOST             above normal windows
    // WS_EX_TOOLWINDOW          no taskbar button, not in Alt+Tab
    // WS_EX_NOACTIVATE          clicking it never takes focus (so it can't minimize a game)
    // WS_EX_NOREDIRECTIONBITMAP no classic drawing surface: everything visible comes from
    //                           the compositor, and the rest of the window is transparent
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"Island", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        return false;
    }

    SetDwmAttribute(m_hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, BOOL{ TRUE });           // allow the blur-behind brush
    SetDwmAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, BOOL{ TRUE });       // show/hide instantly, no fade
    SetDwmAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_DONOTROUND);   // we draw our own shape
    SetDwmAttribute(m_hwnd, DWMWA_BORDER_COLOR, COLORREF{ DWMWA_COLOR_NONE });    // no Windows 11 border

    m_visuals.Initialize(m_hwnd);
    PlaceOnPrimaryMonitor();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    return true;
}

LRESULT CALLBACK IslandWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Windows only knows about the HWND, so we stash a pointer to our C++
    // object on the window when it's created and fetch it for every message.
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<IslandWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (auto* self = reinterpret_cast<IslandWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT IslandWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        // A click must never take focus away from the app you're using.
        return MA_NOACTIVATE;

    case WM_DISPLAYCHANGE:  // monitor plugged/unplugged, resolution or primary monitor changed
    case WM_DPICHANGED:     // display scale changed, e.g. 125 % -> 175 %
    case WM_SETTINGCHANGE:  // display settings changed
        SchedulePlacement();
        return 0;

    case WM_TIMER:
        if (wParam == kPlacementTimer)
        {
            KillTimer(hwnd, kPlacementTimer);
            PlaceOnPrimaryMonitor();
            return 0;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        m_hwnd = nullptr;
        break;
    }

    if (message == AppQuitMessage())
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void IslandWindow::PlaceOnPrimaryMonitor()
{
    // The primary monitor is the one that contains the point (0, 0).
    HMONITOR monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(monitor, &info);

    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const float scale = static_cast<float>(dpiX) / USER_DEFAULT_SCREEN_DPI;

    const int width = static_cast<int>(std::lround(kPillWidth * scale));
    const int pillHeight = static_cast<int>(std::lround(kPillHeight * scale));
    const int topGap = static_cast<int>(std::lround(kTopGap * scale));
    const RECT& screen = info.rcMonitor;
    const int x = screen.left + (screen.right - screen.left - width) / 2;

    // The window starts at the very top edge (the pill floats topGap below it),
    // so pushing the mouse against the top of the screen still lands on the island.
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, screen.top, width, topGap + pillHeight, SWP_NOACTIVATE);
    m_visuals.Layout(static_cast<float>(topGap), static_cast<float>(width), static_cast<float>(pillHeight));
}

void IslandWindow::SchedulePlacement()
{
    // Calling SetTimer again with the same ID restarts it, so a burst of
    // display messages results in a single reposition.
    SetTimer(m_hwnd, kPlacementTimer, kPlacementDelayMs, nullptr);
}
