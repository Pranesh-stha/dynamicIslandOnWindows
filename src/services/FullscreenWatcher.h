#pragma once

#include <windows.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "services/WindowClassifier.h"

// Watches what's in front of the primary monitor and reports whether the
// island must hide (something covers the whole screen) or go slim (a
// maximized window is under it).
//
// It looks at two windows: the foreground window (if it's on the primary
// monitor), and the top app window directly under the island's spot. The
// second one keeps the island hidden over a fullscreen video while you click
// something on the other monitor.
//
// Everything is event-driven. Windows tells us when the foreground changes,
// when a watched window moves or resizes, and when the taskbar's own
// fullscreen detection fires. Nothing is polled, and other processes are
// never touched: no injection, no input hooks, no process handles.
class FullscreenWatcher
{
public:
    struct State
    {
        bool hide = false;
        bool slim = false;
        bool operator==(const State&) const = default;
    };

    FullscreenWatcher() = default;
    FullscreenWatcher(const FullscreenWatcher&) = delete;
    FullscreenWatcher& operator=(const FullscreenWatcher&) = delete;
    ~FullscreenWatcher();

    // onMoveSize(true/false): the user started/stopped dragging or resizing a window.
    bool Start(HINSTANCE instance, std::function<void(State)> onChange, std::function<void(bool)> onMoveSize);
    void SetBlocklist(std::vector<std::wstring> lowerCaseExeNames);
    void Reevaluate();
    State Current() const { return m_state; }

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
    void WatchWindows(HWND first, HWND second);
    bool IsBlocked(HWND window);

    HWND m_hwnd{};
    HWINEVENTHOOK m_foregroundHook{};
    HWINEVENTHOOK m_moveSizeHook{};
    HWINEVENTHOOK m_minimizeHook{};
    std::array<HWND, 2> m_watched{};
    std::array<ThreadHooks, 2> m_threadHooks{};
    bool m_moveSizeActive = false;
    bool m_evaluatePending = false;
    bool m_appBarRegistered = false;
    State m_state;
    std::function<void(State)> m_onChange;
    std::function<void(bool)> m_onMoveSize;
    std::vector<std::wstring> m_blocklist;
    HWND m_blockCheckedWindow{};  // the last window checked against the blocklist, and the answer
    bool m_blockCheckedResult = false;
};
