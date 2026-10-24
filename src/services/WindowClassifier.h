#pragma once

#include <windows.h>

// What the window in front of the island's monitor looks like, which decides
// how the island behaves: Normal = show the pill, Maximized = slim line,
// Fullscreen = hide.
enum class FrontKind
{
    Normal,
    Maximized,
    Fullscreen,
};

// Everything the decision needs, gathered from a real window by
// FullscreenWatcher (or written by hand in the unit tests).
struct WindowFacts
{
    RECT rect{};           // GetWindowRect, in physical pixels
    LONG style = 0;        // GWL_STYLE
    LONG exStyle = 0;      // GWL_EXSTYLE
    bool visible = true;
    bool minimized = false;
    bool maximized = false;  // IsZoomed
    bool cloaked = false;    // hidden by DWM, e.g. an app on another virtual desktop
};

// `monitor` is the full rectangle of the island's monitor (not the work area).
//
// Fullscreen: the window covers the whole monitor and is not an ordinary
// maximized window with a title bar or resize border. That catches exclusive
// fullscreen, borderless "windowed fullscreen" games, F11 browsers and
// fullscreen video.
//
// Maximized: a maximized window on this monitor. With an auto-hide taskbar,
// a maximized window also covers the whole monitor (its invisible resize
// borders even stick out past the edges), so the frame style is what tells
// the two apart.
FrontKind ClassifyWindow(const WindowFacts& window, const RECT& monitor);
