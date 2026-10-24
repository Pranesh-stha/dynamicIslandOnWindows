#pragma once

// The island's states (PLAN.md §5):
//
//   Hidden  <---- something fullscreen ----  (from any state)
//     | fullscreen gone, after the show delay
//     v
//   Tucked: out of sight, like an auto-hide taskbar (only with auto-hide on)
//     | the mouse touches the top edge   ^ mouse gone ~300 ms
//     v                                  |
//   Idle: small pill --- mouse rests ~150 ms --->  Peek: wider
//     ^                                             | click
//     | mouse gone ~300 ms                          v
//     +------------------------------------------- Expanded: control center
//
// With auto-hide on, "mouse gone" from Peek or Expanded goes straight back
// to Tucked. With it off, the island rests as the pill (Idle).
//
// This class is only the rules. It never touches the window: it asks its
// Host to start and stop timers and to show a new state, which keeps it easy
// to test.
enum class IslandState
{
    Hidden,
    Tucked,
    Idle,
    Peek,
    Expanded,
};

enum class IslandTimer
{
    HoverIntent,  // the mouse has rested on the pill long enough to Peek
    Leave,        // the mouse has been gone long enough to collapse
    Show,         // fullscreen has been gone long enough to come back
};

class StateMachine
{
public:
    struct Host
    {
        virtual void StartTimer(IslandTimer timer, int milliseconds) = 0;
        virtual void StopTimer(IslandTimer timer) = 0;
        virtual void EnterState(IslandState from, IslandState to) = 0;

    protected:
        ~Host() = default;
    };

    struct Timing
    {
        int hoverIntentMs = 150;
        int leaveDelayMs = 300;
        int expandedLeaveDelayMs = 500;
        int showDelayMs = 500;
    };

    explicit StateMachine(Host& host) : m_host(host) {}

    void SetTiming(const Timing& timing) { m_timing = timing; }
    IslandState State() const { return m_state; }
    bool PointerInside() const { return m_pointerInside; }

    // Auto-hide: rest out of sight (Tucked) until the mouse touches the top edge.
    void SetAutoHide(bool autoHide);
    IslandState Resting() const { return m_autoHide ? IslandState::Tucked : IslandState::Idle; }

    // Inputs
    void PointerEntered();
    void PointerLeft();
    void PointerCaptured(bool captured);  // a control holds the mouse, e.g. a slider being dragged
    void BackgroundClicked();             // a click on the island, not on a control
    void WindowDragChanged(bool active);  // the user is dragging some window around
    void FullscreenChanged(bool fullscreen, bool immediately = false);
    void OpenExpanded(int leaveDelayMs);  // e.g. from the tray menu
    void TimerElapsed(IslandTimer timer);

    // Pinned: the island stays in its state when the mouse leaves. For
    // screenshots (Island.exe --pose) and for tuning settings.json.
    void SetPinned(bool pinned);
    // Goes straight to Tucked, Idle, Peek or Expanded (not while hidden).
    void ShowDirectly(IslandState state);

private:
    void GoTo(IslandState state);
    void StartLeaveTimer();

    Host& m_host;
    Timing m_timing;
    IslandState m_state = IslandState::Hidden;
    bool m_pointerInside = false;
    bool m_captured = false;
    bool m_windowDrag = false;
    bool m_ignoreHoverUntilLeave = false;
    bool m_fullscreen = true;
    bool m_pinned = false;
    bool m_autoHide = false;
};
