#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "services/WindowClassifier.h"

// Watches what's in front of each screen and reports, per screen, whether
// the island must hide there (something covers the whole screen: no island
// and no edge sensor on it) or go slim (a maximized window is under its spot).
// A game on one screen leaves the other screens alone (PLAN.md Phase 9).
//
// For each screen it looks at the foreground window (if it's on that screen)
// and the top app window directly under that screen's island spot. The
// second one keeps a screen "hidden" over a fullscreen video while you click
// something on another screen.
//
// Everything is event-driven. Windows tells us when the foreground changes,
// when a watched window moves or resizes, and when the taskbar's own
// fullscreen detection fires. Nothing is polled, and other processes are
// never touched: no injection, no input hooks, no process handles.
class FullscreenWatcher
{
public:
    struct Screen
    {
        HMONITOR monitor = nullptr;
        RECT rect{};           // the whole screen, in physical pixels
        bool primary = false;  // the main screen
        bool hide = false;
        bool slim = false;
        bool operator==(const Screen& other) const
        {
            return monitor == other.monitor && EqualRect(&rect, &other.rect) && primary == other.primary &&
                   hide == other.hide && slim == other.slim;
        }
    };

    FullscreenWatcher() = default;
    FullscreenWatcher(const FullscreenWatcher&) = delete;
    FullscreenWatcher& operator=(const FullscreenWatcher&) = delete;
    ~FullscreenWatcher();

    // onChange: every screen, the main one first, when any of them changed.
    // onMoveSize(true/false): the user started/stopped dragging or resizing a window.
    // onChecked: after every look at the windows (the front window may have
    // changed), whether or not anything changed.
    bool Start(HINSTANCE instance, std::function<void(const std::vector<Screen>&)> onChange,
               std::function<void(bool)> onMoveSize, std::function<void()> onChecked);
    void SetBlocklist(std::vector<std::wstring> lowerCaseExeNames);
    void Reevaluate();
    const std::vector<Screen>& Screens() const { return m_screens; }

private:
    struct ThreadHooks
    {
        DWORD thread = 0;
        HWINEVENTHOOK location{};
        HWINEVENTHOOK lifetime{};
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                                    LONG idChild, DWORD thread, DWORD time);

    void HandleWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild);
    void ScheduleEvaluate();
    void Evaluate();
    void WatchWindows(const std::vector<HWND>& windows);
    bool IsBlocked(HWND window);

    HWND m_hwnd{};
    HWINEVENTHOOK m_foregroundHook{};
    HWINEVENTHOOK m_moveSizeHook{};
    HWINEVENTHOOK m_minimizeHook{};
    std::vector<HWND> m_watched;              // the front window and the one under each screen's island spot
    std::vector<ThreadHooks> m_threadHooks;   // one per thread those windows belong to
    bool m_moveSizeActive = false;
    bool m_evaluatePending = false;
    bool m_appBarRegistered = false;
    std::vector<Screen> m_screens;
    std::function<void(const std::vector<Screen>&)> m_onChange;
    std::function<void(bool)> m_onMoveSize;
    std::function<void()> m_onChecked;
    std::vector<std::wstring> m_blocklist;
    HWND m_blockCheckedWindow{};  // the last window checked against the blocklist, and the answer
    bool m_blockCheckedResult = false;
};
