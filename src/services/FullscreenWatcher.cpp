#include "services/FullscreenWatcher.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <initializer_list>

#include "platform/Log.h"

namespace
{
    constexpr wchar_t kClassName[] = L"Island.FullscreenWatcher";
    constexpr UINT kAppBarMessage = WM_APP + 10;

    // Moving or resizing a window sends a burst of events; we look at most
    // once per this interval while that happens.
    constexpr UINT_PTR kEvaluateTimer = 1;
    constexpr UINT kEvaluateDelayMs = 30;

    // Win event callbacks carry no "this" pointer, and there's only one watcher.
    FullscreenWatcher* s_instance = nullptr;

    bool HasClass(HWND window, std::initializer_list<const wchar_t*> names)
    {
        wchar_t className[64]{};
        GetClassNameW(window, className, ARRAYSIZE(className));
        return std::any_of(names.begin(), names.end(),
            [&](const wchar_t* name) { return wcscmp(className, name) == 0; });
    }

    // The desktop itself. When you click it, nothing covers the screen.
    bool IsDesktop(HWND window)
    {
        return HasClass(window, { L"Progman", L"WorkerW" });
    }

    // Shell surfaces that briefly take the foreground: the taskbar, Start,
    // search, Alt+Tab and Task View. They don't say anything about what's on
    // the screen behind them.
    bool IsShellSurface(HWND window)
    {
        return HasClass(window, { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
            L"TopLevelWindowForOverflowXamlIsland", L"Windows.UI.Core.CoreWindow",
            L"XamlExplorerHostIslandWindow", L"ForegroundStaging", L"MultitaskingViewFrame",
            L"TaskListThumbnailWnd" });
    }

    bool IsOwnWindow(HWND window)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        return processId == GetCurrentProcessId();
    }

    bool IsCloaked(HWND window)
    {
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return cloaked != 0;
    }

    WindowFacts FactsOf(HWND window)
    {
        WindowFacts facts;
        GetWindowRect(window, &facts.rect);
        facts.style = GetWindowLongW(window, GWL_STYLE);
        facts.exStyle = GetWindowLongW(window, GWL_EXSTYLE);
        facts.visible = IsWindowVisible(window) != FALSE;
        facts.minimized = IsIconic(window) != FALSE;
        facts.maximized = IsZoomed(window) != FALSE;
        facts.cloaked = IsCloaked(window);
        return facts;
    }

    // The top app window at the island's spot (top-center of the monitor),
    // walking the z-order from the top. Always-on-top windows, tool windows
    // and click-through windows are skipped: overlays (NVIDIA, Game Bar,
    // screen recorders) are exactly those, and they mustn't hide the island.
    HWND FindWindowUnderIsland(const RECT& monitor)
    {
        const POINT spot{ monitor.left + (monitor.right - monitor.left) / 2, monitor.top };
        for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT))
        {
            const LONG exStyle = GetWindowLongW(window, GWL_EXSTYLE);
            if (!IsWindowVisible(window) || IsIconic(window) ||
                (exStyle & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)))
            {
                continue;
            }
            RECT rect{};
            GetWindowRect(window, &rect);
            if (!PtInRect(&rect, spot) || IsCloaked(window) || IsOwnWindow(window))
            {
                continue;
            }
            if (IsDesktop(window))
            {
                return nullptr;  // reached the desktop: nothing is in front
            }
            if (!IsShellSurface(window))
            {
                return window;
            }
        }
        return nullptr;
    }

    const wchar_t* KindName(FrontKind kind)
    {
        switch (kind)
        {
        case FrontKind::Maximized: return L"maximized";
        case FrontKind::Fullscreen: return L"fullscreen";
        default: return L"normal";
        }
    }

    std::wstring ClassOf(HWND window)
    {
        wchar_t className[64]{};
        if (window)
        {
            GetClassNameW(window, className, ARRAYSIZE(className));
        }
        return window ? className : L"-";
    }

    BOOL CALLBACK AddScreen(HMONITOR monitor, HDC, LPRECT, LPARAM data)
    {
        MONITORINFO info{ sizeof(info) };
        if (GetMonitorInfoW(monitor, &info))
        {
            auto& screens = *reinterpret_cast<std::vector<FullscreenWatcher::Screen>*>(data);
            FullscreenWatcher::Screen screen;
            screen.monitor = monitor;
            screen.rect = info.rcMonitor;
            screen.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            screens.push_back(screen);
        }
        return TRUE;
    }

    // Every screen, the main one first.
    std::vector<FullscreenWatcher::Screen> ListScreens()
    {
        std::vector<FullscreenWatcher::Screen> screens;
        EnumDisplayMonitors(nullptr, nullptr, AddScreen, reinterpret_cast<LPARAM>(&screens));
        std::stable_partition(screens.begin(), screens.end(), [](const FullscreenWatcher::Screen& screen) { return screen.primary; });
        return screens;
    }

    std::wstring ScreenName(const FullscreenWatcher::Screen& screen)
    {
        if (screen.primary)
        {
            return L"main screen";
        }
        return std::format(L"screen {}x{} at ({}, {})", screen.rect.right - screen.rect.left, screen.rect.bottom - screen.rect.top,
            screen.rect.left, screen.rect.top);
    }
}

FullscreenWatcher::~FullscreenWatcher()
{
    WatchWindows({});
    for (HWINEVENTHOOK hook : { m_foregroundHook, m_moveSizeHook, m_minimizeHook })
    {
        if (hook)
        {
            UnhookWinEvent(hook);
        }
    }
    if (m_appBarRegistered)
    {
        APPBARDATA appBar{ sizeof(appBar), m_hwnd };
        SHAppBarMessage(ABM_REMOVE, &appBar);
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
    }
    s_instance = nullptr;
}

bool FullscreenWatcher::Start(HINSTANCE instance, std::function<void(const std::vector<Screen>&)> onChange,
                              std::function<void(bool)> onMoveSize, std::function<void()> onChecked)
{
    s_instance = this;
    m_onChange = std::move(onChange);
    m_onMoveSize = std::move(onMoveSize);
    m_onChecked = std::move(onChecked);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // A hidden window at (0, 0), which is on the primary monitor. It receives
    // the taskbar's fullscreen notice and our timer.
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Island fullscreen watcher", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        return false;
    }

    // Registering as an "app bar" (like the taskbar) without reserving any
    // screen space gets us ABN_FULLSCREENAPP: the same signal that makes the
    // taskbar hide when something goes fullscreen.
    APPBARDATA appBar{ sizeof(appBar), m_hwnd, kAppBarMessage };
    m_appBarRegistered = SHAppBarMessage(ABM_NEW, &appBar) != FALSE;

    // "Out of context" win event hooks: Windows queues the events to this
    // thread's message loop. Nothing is loaded into other processes, and
    // unlike input hooks they don't slow down the mouse or keyboard.
    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    m_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnWinEvent, 0, 0, flags);
    m_moveSizeHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0, flags);
    m_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnWinEvent, 0, 0, flags);

    Log::Info(L"fullscreen watcher started (appbar {})", m_appBarRegistered ? L"ok" : L"FAILED");
    Evaluate();
    return m_foregroundHook != nullptr;
}

void FullscreenWatcher::SetBlocklist(std::vector<std::wstring> lowerCaseExeNames)
{
    m_blocklist = std::move(lowerCaseExeNames);
    m_blockCheckedWindow = nullptr;
    Evaluate();
}

void FullscreenWatcher::Reevaluate()
{
    Evaluate();
}

LRESULT CALLBACK FullscreenWatcher::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<FullscreenWatcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
    {
        switch (message)
        {
        case kAppBarMessage:
            if (wParam == ABN_FULLSCREENAPP)
            {
                Log::Info(L"taskbar says: fullscreen app {}", lParam ? L"opened" : L"closed");
                self->Evaluate();
            }
            return 0;

        case WM_TIMER:
            if (wParam == kEvaluateTimer)
            {
                self->Evaluate();
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            self->ScheduleEvaluate();
            break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void CALLBACK FullscreenWatcher::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (s_instance)
    {
        s_instance->HandleWinEvent(event, hwnd, idObject, idChild);
    }
}

void FullscreenWatcher::HandleWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild)
{
    // Location events also fire for the text caret and other parts of a
    // window. Only the window itself matters.
    const bool isWatchedWindow = idObject == OBJID_WINDOW && idChild == CHILDID_SELF && hwnd &&
                                 std::find(m_watched.begin(), m_watched.end(), hwnd) != m_watched.end();
    switch (event)
    {
    case EVENT_SYSTEM_FOREGROUND:
    case EVENT_SYSTEM_MINIMIZESTART:
    case EVENT_SYSTEM_MINIMIZEEND:
        Evaluate();  // immediately: hiding for a game must be fast
        break;

    case EVENT_SYSTEM_MOVESIZESTART:
        m_moveSizeActive = true;
        if (m_onMoveSize)
        {
            m_onMoveSize(true);
        }
        break;

    case EVENT_SYSTEM_MOVESIZEEND:
        m_moveSizeActive = false;
        if (m_onMoveSize)
        {
            m_onMoveSize(false);
        }
        Evaluate();
        break;

    case EVENT_OBJECT_LOCATIONCHANGE:
        if (isWatchedWindow && !m_moveSizeActive)
        {
            ScheduleEvaluate();
        }
        break;

    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_HIDE:
        if (isWatchedWindow)
        {
            Evaluate();
        }
        break;
    }
}

void FullscreenWatcher::ScheduleEvaluate()
{
    if (!m_evaluatePending)
    {
        m_evaluatePending = true;
        SetTimer(m_hwnd, kEvaluateTimer, kEvaluateDelayMs, nullptr);
    }
}

void FullscreenWatcher::Evaluate()
{
    m_evaluatePending = false;
    KillTimer(m_hwnd, kEvaluateTimer);

    // The foreground window, if it's an app (not the desktop or the taskbar).
    // This catches exclusive-fullscreen games, which are always-on-top windows.
    HWND foreground = GetForegroundWindow();
    HWND frontApp = foreground && !IsOwnWindow(foreground) && !IsDesktop(foreground) && !IsShellSurface(foreground)
        ? foreground : nullptr;
    const HMONITOR frontScreen = frontApp ? MonitorFromWindow(frontApp, MONITOR_DEFAULTTONULL) : nullptr;
    const HMONITOR foregroundScreen = foreground ? MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST) : nullptr;

    // What Windows itself knows: exclusive D3D fullscreen and presentation mode.
    QUERY_USER_NOTIFICATION_STATE notificationState{};
    const bool known = SUCCEEDED(SHQueryUserNotificationState(&notificationState));
    const bool presentation = known && notificationState == QUNS_PRESENTATION_MODE;
    const bool exclusive = known && notificationState == QUNS_RUNNING_D3D_FULL_SCREEN;
    // The blocklist from settings.json, for anything the checks above miss.
    const bool blocked = foreground && IsBlocked(foreground);

    // Each screen on its own: the front window if it's there, and the top
    // app window at that screen's island spot.
    std::vector<Screen> screens = ListScreens();
    std::vector<HWND> watched{ frontApp };
    for (Screen& screen : screens)
    {
        const HWND under = FindWindowUnderIsland(screen.rect);
        watched.push_back(under);
        ScreenFacts facts;
        const bool frontHere = frontApp && frontScreen == screen.monitor;
        facts.front = frontHere ? ClassifyWindow(FactsOf(frontApp), screen.rect) : FrontKind::Normal;
        facts.under = under ? ClassifyWindow(FactsOf(under), screen.rect) : FrontKind::Normal;
        facts.exclusiveFullscreen = exclusive && foregroundScreen == screen.monitor;
        facts.blocked = blocked && foregroundScreen == screen.monitor;
        facts.presentation = presentation;
        const ScreenDecision decision = DecideScreen(facts);
        screen.hide = decision.hide;
        screen.slim = decision.slim;

        const auto before = std::find_if(m_screens.begin(), m_screens.end(), [&](const Screen& s) { return s.monitor == screen.monitor; });
        if (before == m_screens.end() || before->hide != screen.hide || before->slim != screen.slim)
        {
            Log::Info(L"{}: front: {} ({}), under island: {} ({}) -> {}", ScreenName(screen),
                ClassOf(frontHere ? frontApp : nullptr), KindName(facts.front), ClassOf(under), KindName(facts.under),
                screen.hide ? L"HIDE" : screen.slim ? L"slim" : L"pill");
        }
    }
    WatchWindows(watched);

    if (screens != m_screens)
    {
        m_screens = std::move(screens);
        if (m_onChange)
        {
            m_onChange(m_screens);
        }
    }
    if (m_onChecked)
    {
        m_onChecked();
    }
}

void FullscreenWatcher::WatchWindows(const std::vector<HWND>& windows)
{
    // Watch each window's own thread for moves, resizes, hides and closes.
    // Limiting the hooks to those threads keeps the events rare. Even mouse
    // pointer movement is reported by a system thread, not these ones.
    m_watched = windows;

    struct Wanted
    {
        DWORD thread;
        DWORD process;
    };
    std::vector<Wanted> wanted;
    for (HWND window : windows)
    {
        DWORD process = 0;
        const DWORD thread = window ? GetWindowThreadProcessId(window, &process) : 0;
        if (thread && std::none_of(wanted.begin(), wanted.end(), [&](const Wanted& w) { return w.thread == thread; }))
        {
            wanted.push_back({ thread, process });
        }
    }

    std::erase_if(m_threadHooks, [&](const ThreadHooks& hooks)
    {
        const bool keep = std::any_of(wanted.begin(), wanted.end(), [&](const Wanted& w) { return w.thread == hooks.thread; });
        if (!keep)
        {
            UnhookWinEvent(hooks.location);
            UnhookWinEvent(hooks.lifetime);
        }
        return !keep;
    });
    for (const Wanted& w : wanted)
    {
        if (std::any_of(m_threadHooks.begin(), m_threadHooks.end(), [&](const ThreadHooks& hooks) { return hooks.thread == w.thread; }))
        {
            continue;
        }
        ThreadHooks hooks;
        hooks.thread = w.thread;
        hooks.location = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
            nullptr, OnWinEvent, w.process, w.thread, WINEVENT_OUTOFCONTEXT);
        hooks.lifetime = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
            nullptr, OnWinEvent, w.process, w.thread, WINEVENT_OUTOFCONTEXT);
        m_threadHooks.push_back(hooks);
    }
}

bool FullscreenWatcher::IsBlocked(HWND window)
{
    if (m_blocklist.empty())
    {
        return false;
    }
    if (window == m_blockCheckedWindow)
    {
        return m_blockCheckedResult;  // same window as last time: its process can't have changed
    }
    m_blockCheckedWindow = window;
    m_blockCheckedResult = false;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);

    // A snapshot of the process list gives exe names without opening a
    // handle to any process (anti-cheat software watches for those).
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    PROCESSENTRY32W entry{ sizeof(entry) };
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
    {
        if (entry.th32ProcessID == processId)
        {
            std::wstring name = entry.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            m_blockCheckedResult = std::find(m_blocklist.begin(), m_blocklist.end(), name) != m_blocklist.end();
            break;
        }
    }
    CloseHandle(snapshot);
    return m_blockCheckedResult;
}
