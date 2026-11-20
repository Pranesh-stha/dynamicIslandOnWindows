#pragma once

#include <windows.h>

#include <functional>
#include <vector>

// Where the tucked-away island's strip is on a screen: top-centre,
// `revealWidthDip` wide (an even number of pixels, so it shares the screen's
// centre pixel) and 2 px tall (3 at 175 %). The island uses it for itself,
// EdgeSensors for the other screens. Pure, unit-tested.
RECT StripRect(const RECT& monitor, float scale, float revealWidthDip);

// Is a visible window above `window` in the z-order overlapping it? Then the
// mouse can't reach it (e.g. Chromium-based apps lifting themselves above
// always-on-top windows, PLAN.md B11).
bool CoveredByAnotherWindow(HWND window);

// An invisible sensor strip at the top-middle of each screen (PLAN.md Phase
// 9): when the mouse touches one, the island comes to that screen. Like the
// island's own tucked strip, a sensor is always on top, never activated and
// draws nothing, and Windows tells us when the mouse touches it, so nothing
// is polled. A sensor is off (really hidden) on the island's own screen and
// while its screen has something fullscreen.
class EdgeSensors
{
public:
    struct Spot
    {
        HMONITOR monitor = nullptr;
        RECT rect{};       // the screen, in physical pixels
        bool off = false;  // the island is there, or something fullscreen
    };

    EdgeSensors() = default;
    EdgeSensors(const EdgeSensors&) = delete;
    EdgeSensors& operator=(const EdgeSensors&) = delete;
    ~EdgeSensors();

    // onTouched(screen, point): the mouse touched that screen's sensor at `point` (screen pixels).
    bool Create(HINSTANCE instance, std::function<void(HMONITOR, POINT)> onTouched);

    // One sensor per spot: made, moved, shown or hidden to match; sensors of
    // screens that are gone are removed.
    void Update(const std::vector<Spot>& spots, float revealWidthDip);

    // Raises any sensor that something has been put above (see IslandWindow::KeepOnTop).
    void KeepOnTop();

private:
    struct Sensor
    {
        HMONITOR monitor = nullptr;
        HWND hwnd = nullptr;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance{};
    std::function<void(HMONITOR, POINT)> m_onTouched;
    std::vector<Sensor> m_sensors;
};
