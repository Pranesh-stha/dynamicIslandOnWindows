#pragma once

#include <windows.h>

#include "render/IslandVisuals.h"

// The island's own window: borderless, transparent, always on top, and it
// never takes focus. It sits at the top-center of the primary monitor and
// follows it when monitors are plugged in, unplugged or rescaled.
class IslandWindow
{
public:
    static constexpr wchar_t kClassName[] = L"Island.Pill";

    IslandWindow() = default;
    IslandWindow(const IslandWindow&) = delete;
    IslandWindow& operator=(const IslandWindow&) = delete;
    ~IslandWindow();

    bool Create(HINSTANCE instance);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void PlaceOnPrimaryMonitor();
    void SchedulePlacement();

    HWND m_hwnd{};
    IslandVisuals m_visuals;
};
