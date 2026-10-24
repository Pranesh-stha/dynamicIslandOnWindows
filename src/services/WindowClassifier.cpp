#include "services/WindowClassifier.h"

FrontKind ClassifyWindow(const WindowFacts& window, const RECT& monitor)
{
    // Click-through overlays (WS_EX_TRANSPARENT) never count: the NVIDIA and
    // Game Bar overlays are full-screen, invisible windows of that kind.
    if (!window.visible || window.minimized || window.cloaked || (window.exStyle & WS_EX_TRANSPARENT))
    {
        return FrontKind::Normal;
    }

    const RECT& r = window.rect;
    const bool coversMonitor = r.left <= monitor.left && r.top <= monitor.top &&
                               r.right >= monitor.right && r.bottom >= monitor.bottom;

    // A real maximized window keeps its frame styles even when an app draws
    // its own title bar (Chrome, VS Code). Going fullscreen removes them.
    const bool hasFrame = (window.style & WS_CAPTION) == WS_CAPTION || (window.style & WS_THICKFRAME);

    if (coversMonitor)
    {
        return window.maximized && hasFrame ? FrontKind::Maximized : FrontKind::Fullscreen;
    }

    if (window.maximized)
    {
        // Maximized on this monitor: it contains the top-center point,
        // which is where the island sits.
        const LONG centerX = monitor.left + (monitor.right - monitor.left) / 2;
        const bool onThisMonitor = r.left <= centerX && centerX < r.right &&
                                   r.top <= monitor.top && monitor.top < r.bottom;
        if (onThisMonitor)
        {
            return FrontKind::Maximized;
        }
    }
    return FrontKind::Normal;
}
