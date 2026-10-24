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

    // Creates the window hidden; call SetFullscreenHidden to show it.
    bool Create(HINSTANCE instance);

    // Something fullscreen covers the primary monitor. Hiding is instant;
    // showing waits `showDelayMs` unless `immediately` is set.
    void SetFullscreenHidden(bool hidden, bool immediately = false);
    void SetShowDelay(int showDelayMs) { m_showDelayMs = showDelayMs; }

    // A maximized window is under the island.
    void SetSlim(bool slim);

    // The user is dragging or resizing some window.
    void SetWindowDragActive(bool active);

    HWND Handle() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void PlaceOnPrimaryMonitor();
    void SchedulePlacement();
    void ShowNow();

    HWND m_hwnd{};
    IslandVisuals m_visuals;
    bool m_hidden = true;
    bool m_slim = false;
    bool m_windowDragActive = false;
    int m_showDelayMs = 500;
};
