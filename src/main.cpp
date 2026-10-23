// Island - Phase 0: a plain window that proves the toolchain works.
//
// What this file shows:
//   1. wWinMain       - where every windowed Windows app starts.
//   2. Window class   - a template that says "windows of this kind are handled by WindowProc".
//   3. Message loop   - the app sleeps until Windows sends it a message, handles it, sleeps again.
//   4. C++/WinRT      - calling modern Windows APIs from C++ (we read the Windows version
//                       and open the Display settings page, which the island will do later).

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>

#include <cstdint>
#include <format>
#include <string>

namespace
{
    std::wstring g_text;

    // DeviceFamilyVersion is one big number that packs four 16-bit parts:
    // major.minor.build.revision (for example 10.0.26200.6899).
    std::wstring ReadWindowsVersion()
    {
        using winrt::Windows::System::Profile::AnalyticsInfo;
        const std::uint64_t v = std::stoull(std::wstring{ AnalyticsInfo::VersionInfo().DeviceFamilyVersion() });
        return std::format(L"{}.{}.{}.{}", (v >> 48) & 0xFFFF, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF);
    }

    // Windows calls this function for every message sent to our window.
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            InflateRect(&rc, -24, -24);
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, g_text.c_str(), -1, &rc, DT_CENTER | DT_WORDBREAK);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONUP:
            // Fire-and-forget: Windows opens Settings > Display on its own.
            winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri{ L"ms-settings:display" });
            return 0;

        case WM_DESTROY:
            // The window is closing: ask the message loop to stop.
            PostQuitMessage(0);
            return 0;
        }

        // Anything we don't handle gets Windows' default behavior (moving, resizing, ...).
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand)
{
    // The UI thread must be "single-threaded apartment" for WinRT UI APIs.
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    g_text = std::format(
        L"Island - Phase 0\n\n"
        L"Windows version (read with C++/WinRT): {}\n\n"
        L"Click anywhere to open Display settings.",
        ReadWindowsVersion());

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"IslandPhase0";
    RegisterClassExW(&windowClass);

    HWND hwnd = CreateWindowExW(
        0, windowClass.lpszClassName, L"Island (Phase 0)", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 240,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        return 1;
    }
    ShowWindow(hwnd, showCommand);

    // The message loop. GetMessageW sleeps (0% CPU) until a message arrives,
    // and returns 0 once PostQuitMessage has been called.
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
