#include "platform/EdgeSensors.h"

#include <dwmapi.h>
#include <ShellScalingApi.h>

#include <algorithm>
#include <cmath>

#include "platform/Log.h"

namespace
{
    constexpr wchar_t kClassName[] = L"Island.EdgeSensor";

    float ScaleOf(HMONITOR monitor)
    {
        UINT dpiX = USER_DEFAULT_SCREEN_DPI;
        UINT dpiY = USER_DEFAULT_SCREEN_DPI;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        return static_cast<float>(dpiX) / USER_DEFAULT_SCREEN_DPI;
    }

    void LogSensor(const RECT& screen, bool on)
    {
        Log::Info(L"sensor on the screen {}x{} at ({}, {}): {}", screen.right - screen.left, screen.bottom - screen.top,
            screen.left, screen.top, on ? L"on" : L"off");
    }
}

RECT StripRect(const RECT& monitor, float scale, float revealWidthDip)
{
    const LONG width = static_cast<LONG>(2.0f * std::round(revealWidthDip * scale / 2.0f));
    const LONG height = static_cast<LONG>(std::max(2.0f, std::floor(scale * 2.0f)));
    const LONG left = monitor.left + (monitor.right - monitor.left) / 2 - width / 2;
    return { left, monitor.top, left + width, monitor.top + height };
}

bool CoveredByAnotherWindow(HWND window)
{
    RECT mine{};
    GetWindowRect(window, &mine);
    for (HWND above = GetWindow(window, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV))
    {
        if (!IsWindowVisible(above))
        {
            continue;
        }
        BOOL cloaked = FALSE;  // e.g. Store apps' frames on other virtual desktops: "visible" but not shown
        DwmGetWindowAttribute(above, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        RECT rect{}, overlap{};
        if (!cloaked && GetWindowRect(above, &rect) && IntersectRect(&overlap, &mine, &rect))
        {
            return true;
        }
    }
    return false;
}

EdgeSensors::~EdgeSensors()
{
    for (const Sensor& sensor : m_sensors)
    {
        DestroyWindow(sensor.hwnd);
    }
}

bool EdgeSensors::Create(HINSTANCE instance, std::function<void(HMONITOR, POINT)> onTouched)
{
    m_instance = instance;
    m_onTouched = std::move(onTouched);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void EdgeSensors::Update(const std::vector<Spot>& spots, float revealWidthDip)
{
    // Screens that are gone take their sensors with them.
    std::erase_if(m_sensors, [&](const Sensor& sensor)
    {
        const bool kept = std::any_of(spots.begin(), spots.end(), [&](const Spot& spot) { return spot.monitor == sensor.monitor; });
        if (!kept)
        {
            DestroyWindow(sensor.hwnd);
        }
        return !kept;
    });

    for (const Spot& spot : spots)
    {
        auto sensor = std::find_if(m_sensors.begin(), m_sensors.end(), [&](const Sensor& s) { return s.monitor == spot.monitor; });
        if (sensor == m_sensors.end())
        {
            // Always on top, never activated, no taskbar button, and nothing
            // drawn (no redirection bitmap): invisible, yet the mouse finds it.
            HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                kClassName, L"Island edge sensor", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, m_instance, this);
            if (!hwnd)
            {
                continue;
            }
            m_sensors.push_back({ spot.monitor, hwnd });
            sensor = m_sensors.end() - 1;
        }

        const bool visible = IsWindowVisible(sensor->hwnd) != FALSE;
        if (spot.off)
        {
            if (visible)
            {
                ShowWindow(sensor->hwnd, SW_HIDE);  // really hidden: nothing on top of a game
                LogSensor(spot.rect, false);
            }
            continue;
        }
        const RECT strip = StripRect(spot.rect, ScaleOf(spot.monitor), revealWidthDip);
        RECT now{};
        GetWindowRect(sensor->hwnd, &now);
        if (visible && EqualRect(&now, &strip))
        {
            continue;  // already there; KeepOnTop deals with anything put above it
        }
        SetWindowPos(sensor->hwnd, HWND_TOPMOST, strip.left, strip.top, strip.right - strip.left, strip.bottom - strip.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (!visible)
        {
            LogSensor(spot.rect, true);
        }
    }
}

void EdgeSensors::KeepOnTop()
{
    for (const Sensor& sensor : m_sensors)
    {
        if (IsWindowVisible(sensor.hwnd) && CoveredByAnotherWindow(sensor.hwnd))
        {
            SetWindowPos(sensor.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            Log::Info(L"sensor raised again (a window had been put above it)");
        }
    }
}

LRESULT CALLBACK EdgeSensors::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<EdgeSensors*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;  // never take the focus from what you're doing

    case WM_MOUSEMOVE:
        if (self && self->m_onTouched)
        {
            const auto sensor = std::find_if(self->m_sensors.begin(), self->m_sensors.end(), [&](const Sensor& s) { return s.hwnd == hwnd; });
            if (sensor != self->m_sensors.end())
            {
                POINT point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                ClientToScreen(hwnd, &point);
                const HMONITOR monitor = sensor->monitor;
                self->m_onTouched(monitor, point);  // may hide this sensor (the island comes here), never destroys it
            }
        }
        return 0;

    case WM_DPICHANGED:
        return 0;  // placed in physical pixels by Update, at its screen's own scale
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
