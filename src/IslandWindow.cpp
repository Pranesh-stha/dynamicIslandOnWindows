#include "IslandWindow.h"

#include <dwmapi.h>
#include <ShellScalingApi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "AppMessages.h"
#include "platform/Log.h"
#include "views/ControlCenter.h"
#include "views/ControlKitDemo.h"
#include "views/IdleView.h"
#include "views/PeekView.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace
{
    // Display changes arrive in bursts (docking, closing the lid), so we wait
    // until they settle before repositioning.
    constexpr UINT_PTR kPlacementTimer = 1;
    constexpr UINT kPlacementDelayMs = 250;

    // One Windows timer per StateMachine timer.
    constexpr UINT_PTR kShowTimer = 2;
    constexpr UINT_PTR kHoverTimer = 3;
    constexpr UINT_PTR kLeaveTimer = 4;

    // When the panel is opened from the tray, the mouse isn't on it; give
    // you time to get there before it closes again.
    constexpr int kTrayOpenLeaveDelayMs = 4000;

    UINT_PTR TimerId(IslandTimer timer)
    {
        switch (timer)
        {
        case IslandTimer::HoverIntent: return kHoverTimer;
        case IslandTimer::Leave: return kLeaveTimer;
        default: return kShowTimer;
        }
    }

    const wchar_t* StateName(IslandState state)
    {
        switch (state)
        {
        case IslandState::Hidden: return L"hidden";
        case IslandState::Idle: return L"idle";
        case IslandState::Peek: return L"peek";
        default: return L"expanded";
        }
    }

    template <typename T>
    void SetDwmAttribute(HWND hwnd, DWMWINDOWATTRIBUTE attribute, T value)
    {
        DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
    }
}

IslandWindow::IslandWindow() : m_machine(*this)
{
}

IslandWindow::~IslandWindow()
{
    if (m_uiSettings)
    {
        m_uiSettings.AdvancedEffectsEnabledChanged(m_effectsChangedToken);
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
    }
}

bool IslandWindow::Create(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // WS_EX_TOPMOST             above normal windows
    // WS_EX_TOOLWINDOW          no taskbar button, not in Alt+Tab
    // WS_EX_NOACTIVATE          clicking it never takes focus (so it can't minimize a game)
    // WS_EX_NOREDIRECTIONBITMAP no classic drawing surface: everything visible comes from
    //                           the compositor, and the rest of the window is transparent
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"Island", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        return false;
    }

    SetDwmAttribute(m_hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, BOOL{ TRUE });           // allow the blur-behind brush
    SetDwmAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, BOOL{ TRUE });       // show/hide instantly, no fade
    SetDwmAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_DONOTROUND);   // we draw our own shape
    SetDwmAttribute(m_hwnd, DWMWA_BORDER_COLOR, COLORREF{ DWMWA_COLOR_NONE });    // no Windows 11 border

    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_compositor = wuc::Compositor();
    m_graphics.Initialize(m_compositor);
    m_graphics.OnDeviceReplaced([this] { Rebuild(); });
    m_visuals.Initialize(m_hwnd, m_compositor, m_graphics);

    m_ui.compositor = m_compositor;
    m_ui.graphics = &m_graphics;
    m_ui.motion = &m_motion;
    m_ui.layout = &m_layout;

    // Windows' own appearance settings: the accent colour, and whether
    // transparency effects are on (Settings > Personalization > Colors).
    m_uiSettings = winrt::Windows::UI::ViewManagement::UISettings();
    ReadSystemColors();
    m_effectsChangedToken = m_uiSettings.AdvancedEffectsEnabledChanged([this](auto&&, auto&&)
    {
        // Raised on a background thread; continue on the UI thread.
        m_queue.TryEnqueue([this]
        {
            ReadSystemColors();
            Rebuild();
        });
    });

    const auto content = m_visuals.Content();
    m_idleView = std::make_unique<IdleView>(m_ui, content);
    m_peekView = std::make_unique<PeekView>(m_ui, content, m_actions);
    m_controlCenter = std::make_unique<ControlCenter>(m_ui, content, m_actions);
    m_demoView = std::make_unique<ControlKitDemo>(m_ui, content);

    PlaceOnPrimaryMonitor();
    return true;
}

void IslandWindow::ReadSystemColors()
{
    using winrt::Windows::UI::ViewManagement::UIColorType;
    m_effectsEnabled = m_uiSettings.AdvancedEffectsEnabled();
    // The light accent shade is the one Windows uses on dark backgrounds.
    m_systemAccent = m_uiSettings.GetColorValue(UIColorType::AccentLight2);
    Log::Info(L"transparency effects {}", m_effectsEnabled ? L"on" : L"OFF (solid glass)");
}

winrt::Windows::UI::Color IslandWindow::PickAccent() const
{
    const std::wstring& setting = m_glass.accent;
    if (setting.size() == 7 && setting[0] == L'#')
    {
        const unsigned long rgb = std::wcstoul(setting.c_str() + 1, nullptr, 16);
        return { 255, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
    }
    // A grey accent colour would make "on" look just like "off", so use blue.
    const auto& c = m_systemAccent;
    const int high = std::max({ c.R, c.G, c.B });
    const int low = std::min({ c.R, c.G, c.B });
    const bool grey = high == 0 || static_cast<float>(high - low) / static_cast<float>(high) < 0.2f;
    return grey ? winrt::Windows::UI::Color{ 255, 64, 156, 255 } : c;
}

void IslandWindow::ApplySettings(const Settings& settings)
{
    m_layout = settings.layout;
    m_motion = settings.motion;
    m_glass = settings.glass;
    m_showDelayMs = settings.games.showDelayMs;
    m_machine.SetTiming({ m_motion.hoverIntentMs, m_motion.leaveDelayMs, m_motion.expandedLeaveDelayMs, m_showDelayMs });
    Rebuild();
}

void IslandWindow::Rebuild()
{
    if (m_scale <= 0.0f)
    {
        return;  // not placed on a monitor yet
    }
    // The controls are about to be replaced, so let go of any press.
    if (GetCapture() == m_hwnd)
    {
        ReleaseCapture();
    }
    m_ui.scale = m_scale;
    m_ui.accent = PickAccent();
    m_visuals.ApplyGlass(m_glass, m_effectsEnabled, m_scale);
    m_idleView->Build();
    m_peekView->Build();
    m_controlCenter->Build();
    m_demoView->Build();
    m_windowSize = {};
    ShowState(false);
}

void IslandWindow::SetFullscreenHidden(bool hidden, bool immediately)
{
    m_machine.FullscreenChanged(hidden, immediately);
}

void IslandWindow::SetSlim(bool slimRequested)
{
    m_slimRequested = slimRequested;
    const bool slim = slimRequested && !m_posed;  // a pose always shows the pill
    if (slim == m_slim)
    {
        return;
    }
    m_slim = slim;
    Log::Info(L"island {}", slim ? L"slim (maximized window under it)" : L"pill");
    if (State() == IslandState::Idle)
    {
        ShowState(true);  // morph between the pill and the line
    }
}

void IslandWindow::SetWindowDragActive(bool active)
{
    m_machine.WindowDragChanged(active);
}

void IslandWindow::Pose(IslandPose pose)
{
    m_posed = pose != IslandPose::None;
    Log::Info(L"pose {}", static_cast<int>(pose));
    m_machine.SetPinned(m_posed);
    SetSlim(m_slimRequested);
    if (!m_posed)
    {
        return;
    }

    const IslandState target = pose == IslandPose::Idle ? IslandState::Idle
                             : pose == IslandPose::Peek ? IslandState::Peek
                                                        : IslandState::Expanded;
    const bool showDemo = pose == IslandPose::Demo;
    const bool switchingPanel = target == IslandState::Expanded && State() == IslandState::Expanded && showDemo != m_showDemo;
    m_showDemo = showDemo;
    if (switchingPanel)
    {
        ShowState(true);
    }
    m_machine.ShowDirectly(target);
}

void IslandWindow::SetMedia(const MediaState& media)
{
    m_media = media;
    if (State() != IslandState::Hidden)
    {
        PushMedia();
    }
}

void IslandWindow::PushMedia()
{
    m_idleView->SetMedia(m_media);
    m_peekView->SetMedia(m_media);
    m_controlCenter->SetMedia(m_media);
}

void IslandWindow::SetAudio(const AudioState& audio)
{
    m_audio = audio;
    if (State() != IslandState::Hidden)
    {
        m_controlCenter->SetAudio(audio);
    }
}

void IslandWindow::SetRadios(const RadiosState& radios)
{
    m_radios = radios;
    if (State() != IslandState::Hidden)
    {
        m_controlCenter->SetRadios(radios);
    }
}

void IslandWindow::SetBrightness(const BrightnessState& brightness)
{
    m_brightness = brightness;
    if (State() == IslandState::Hidden)
    {
        return;
    }
    if (m_controlCenter->SetBrightness(brightness))
    {
        // A display came or went: one slider more or less, so the panel's
        // height changes. Rebuild it, and morph to the new height if it's open.
        m_controlCenter->Build();
        if (State() == IslandState::Expanded && ExpandedView() == m_controlCenter.get())
        {
            ShowState(true);
        }
    }
}

void IslandWindow::PushQuickControls()
{
    m_controlCenter->SetAudio(m_audio);
    SetBrightness(m_brightness);
    m_controlCenter->SetRadios(m_radios);
}

void IslandWindow::OpenControlKitDemo()
{
    const bool switching = !m_showDemo && State() == IslandState::Expanded;
    m_showDemo = true;
    if (switching)
    {
        ShowState(true);
    }
    m_machine.OpenExpanded(kTrayOpenLeaveDelayMs);
}

// --- State machine host ------------------------------------------------------

void IslandWindow::StartTimer(IslandTimer timer, int milliseconds)
{
    SetTimer(m_hwnd, TimerId(timer), static_cast<UINT>(std::max(milliseconds, 1)), nullptr);
}

void IslandWindow::StopTimer(IslandTimer timer)
{
    KillTimer(m_hwnd, TimerId(timer));
}

void IslandWindow::EnterState(IslandState from, IslandState to)
{
    Log::Info(L"state: {} -> {}", StateName(from), StateName(to));

    // Clear every hover and press in the view that's going away, so nothing
    // stays highlighted (PLAN.md D7).
    if (View* old = ViewFor(from))
    {
        old->Controls().CancelPress();
        old->Controls().PointerLeave();
    }
    if (GetCapture() == m_hwnd)
    {
        ReleaseCapture();
    }
    m_backgroundPress = false;
    if (from == IslandState::Expanded && !m_posed)
    {
        m_showDemo = false;  // the next click opens the control center again
    }

    switch (to)
    {
    case IslandState::Hidden:
        // Really hidden, not just transparent: any visible window on top of a
        // borderless game makes Windows compose the screen, which costs the
        // game FPS and adds input lag. Nothing animates while hidden.
        ShowWindow(m_hwnd, SW_HIDE);
        m_pointerInside = false;
        ShowState(false);  // come back as the plain pill, without animating while hidden
        Log::Info(L"island hidden");
        break;

    case IslandState::Idle:
        if (from == IslandState::Hidden)
        {
            PushMedia();  // catch up on what changed while hidden
            PushQuickControls();
            ShowState(false);
            ShowNow();
        }
        else
        {
            ShowState(true);
        }
        break;

    default:
        ShowState(true);
        // Give the new view's controls the hover right away, without waiting
        // for the mouse to move.
        if (View* view = InteractiveView(); view && m_pointerInside)
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(m_hwnd, &cursor);
            const auto point = ToLayer(cursor, *view);
            view->Controls().PointerMove(point.x, point.y);
        }
        break;
    }

    if (m_onStateChanged)
    {
        m_onStateChanged(to);
    }
}

void IslandWindow::ShowNow()
{
    // Re-assert "always on top" only here, on our own state change, never in
    // a loop, so we don't fight other always-on-top windows.
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Log::Info(L"island shown");
}

// --- Showing a state ---------------------------------------------------------

View* IslandWindow::ExpandedView() const
{
    return m_showDemo ? static_cast<View*>(m_demoView.get()) : static_cast<View*>(m_controlCenter.get());
}

View* IslandWindow::ViewFor(IslandState state) const
{
    switch (state)
    {
    case IslandState::Peek: return m_peekView.get();
    case IslandState::Expanded: return ExpandedView();
    default: return m_slim ? nullptr : m_idleView.get();  // the slim line shows no content
    }
}

View* IslandWindow::InteractiveView() const
{
    const IslandState state = State();
    return state == IslandState::Peek || state == IslandState::Expanded ? ViewFor(state) : nullptr;
}

IslandShape IslandWindow::ShapeFor(IslandState state) const
{
    const float top = m_ui.Px(m_layout.topGap);
    switch (state)
    {
    case IslandState::Peek:
        return { m_peekView->Width(), m_peekView->Height(), top, m_peekView->Height() / 2.0f };
    case IslandState::Expanded:
    {
        const View* view = ExpandedView();
        return { view->Width(), view->Height(), top, m_ui.Px(m_layout.panelRadius) };
    }
    default:
        if (m_slim)
        {
            // Touches the very top edge: the cursor stops there, so it's easy to hit.
            const float height = std::max(2.0f, m_ui.Px(m_layout.slimHeight));
            return { m_ui.EvenPx(m_layout.slimWidth), height, 0.0f, height / 2.0f };
        }
        return { m_idleView->Width(), m_idleView->Height(), top, m_idleView->Height() / 2.0f };
    }
}

void IslandWindow::ShowState(bool animate)
{
    const IslandState state = State();
    const IslandShape shape = ShapeFor(state);

    View* shown = ViewFor(state);
    for (View* view : { static_cast<View*>(m_idleView.get()), static_cast<View*>(m_peekView.get()),
                        static_cast<View*>(m_controlCenter.get()), static_cast<View*>(m_demoView.get()) })
    {
        const bool visible = view == shown;
        if (!animate || view->Visible() != visible)
        {
            view->SetVisible(visible, animate);
        }
    }

    // The window is never resized during the motion (that's what makes most
    // "island" apps stutter). It grows once, at the start, to fit the target
    // plus a little room for the spring's overshoot; it shrinks to fit
    // exactly once the motion has settled. In between, everything is GPU.
    const float width = shape.width;
    const float height = shape.top + shape.height;
    if (animate)
    {
        const float margin = std::ceil(std::max(shape.width, shape.height) * 0.05f) + 2.0f;
        ResizeWindow(std::max(static_cast<float>(m_windowSize.cx), width + 2.0f * margin),
                     std::max(static_cast<float>(m_windowSize.cy), height + margin));
        m_visuals.MorphTo(shape, m_motion.morph, true, [this, width, height] { ResizeWindow(width, height); });
    }
    else
    {
        m_visuals.MorphTo(shape, m_motion.morph, false);
        ResizeWindow(width, height);
    }
}

void IslandWindow::ResizeWindow(float width, float height)
{
    const int w = static_cast<int>(std::lround(width));
    const int h = static_cast<int>(std::lround(height));
    if (w == m_windowSize.cx && h == m_windowSize.cy)
    {
        return;
    }
    m_windowSize = { w, h };
    // Widths are even, so the window and the pill share the same center pixel.
    const int centerX = m_monitor.left + (m_monitor.right - m_monitor.left) / 2;
    SetWindowPos(m_hwnd, nullptr, centerX - w / 2, m_monitor.top, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
}

Motion::float2 IslandWindow::ToLayer(POINT point, const View& view) const
{
    // Layers hang from the top center of the pill.
    return { static_cast<float>(point.x) - (static_cast<float>(m_windowSize.cx) - view.Width()) / 2.0f,
             static_cast<float>(point.y) - ShapeFor(State()).top };
}

// --- Placement ---------------------------------------------------------------

void IslandWindow::PlaceOnPrimaryMonitor()
{
    // The primary monitor is the one that contains the point (0, 0).
    HMONITOR monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(monitor, &info);

    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const float scale = static_cast<float>(dpiX) / USER_DEFAULT_SCREEN_DPI;

    const RECT& screen = info.rcMonitor;
    if (EqualRect(&screen, &m_monitor) && scale == m_scale)
    {
        return;  // nothing that matters to us changed
    }
    m_monitor = screen;
    Log::Info(L"primary monitor {}x{} at ({}, {}), scale {:.0f} %",
        screen.right - screen.left, screen.bottom - screen.top, screen.left, screen.top, scale * 100.0f);

    if (scale != m_scale)
    {
        m_scale = scale;
        Rebuild();  // redraw text and icons sharply at the new scale
    }
    else
    {
        m_windowSize = {};
        ShowState(false);
    }
}

void IslandWindow::SchedulePlacement()
{
    // Calling SetTimer again with the same ID restarts it, so a burst of
    // display messages results in a single reposition.
    SetTimer(m_hwnd, kPlacementTimer, kPlacementDelayMs, nullptr);
}

// --- Mouse -------------------------------------------------------------------

void IslandWindow::SetPointerInside(bool inside)
{
    if (inside == m_pointerInside)
    {
        return;
    }
    m_pointerInside = inside;
    if (inside)
    {
        m_machine.PointerEntered();
    }
    else
    {
        if (View* view = InteractiveView())
        {
            view->Controls().PointerLeave();
        }
        m_machine.PointerLeft();
    }
}

void IslandWindow::OnMouseMove(POINT point)
{
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const bool inside = PtInRect(&client, point) != FALSE;
    if (inside && GetCapture() != m_hwnd)
    {
        // Ask for WM_MOUSELEAVE. Re-arming it on every move means a quick
        // in-and-out can never leave a highlight stuck on (PLAN.md D7).
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&track);
    }
    SetPointerInside(inside);

    if (View* view = InteractiveView())
    {
        const auto layerPoint = ToLayer(point, *view);
        view->Controls().PointerMove(layerPoint.x, layerPoint.y);
    }
}

void IslandWindow::OnMouseLeave()
{
    // While a control holds the mouse (dragging a slider), WM_MOUSEMOVE keeps
    // coming and tells us where the pointer is.
    if (GetCapture() != m_hwnd)
    {
        SetPointerInside(false);
    }
}

void IslandWindow::OnLeftDown(POINT point)
{
    m_backgroundPress = false;
    if (View* view = InteractiveView())
    {
        const auto layerPoint = ToLayer(point, *view);
        if (view->Controls().PointerDown(layerPoint.x, layerPoint.y))
        {
            // Keep getting mouse messages even if the pointer leaves the window.
            SetCapture(m_hwnd);
            m_machine.PointerCaptured(true);
            return;
        }
    }
    m_backgroundPress = true;
}

void IslandWindow::OnLeftUp(POINT point)
{
    if (View* view = InteractiveView(); view && view->Controls().Pressing())
    {
        const auto layerPoint = ToLayer(point, *view);
        view->Controls().PointerUp(layerPoint.x, layerPoint.y);
    }
    if (GetCapture() == m_hwnd)
    {
        ReleaseCapture();  // -> WM_CAPTURECHANGED
    }
    else if (m_backgroundPress && m_pointerInside)
    {
        m_machine.BackgroundClicked();
    }
    m_backgroundPress = false;

    RECT client{};
    GetClientRect(m_hwnd, &client);
    SetPointerInside(PtInRect(&client, point) != FALSE);
}

void IslandWindow::OnRightUp(POINT point)
{
    if (View* view = InteractiveView())
    {
        const auto layerPoint = ToLayer(point, *view);
        view->Controls().RightClick(layerPoint.x, layerPoint.y);
    }
}

void IslandWindow::OnWheel(POINT screenPoint, int delta)
{
    // Windows sends the wheel to the window under the pointer ("scroll
    // inactive windows"), so this works without the island ever having focus.
    if (View* view = InteractiveView())
    {
        ScreenToClient(m_hwnd, &screenPoint);
        const auto layerPoint = ToLayer(screenPoint, *view);
        view->Controls().Wheel(layerPoint.x, layerPoint.y, static_cast<float>(delta) / WHEEL_DELTA);
    }
}

// --- Window procedure --------------------------------------------------------

LRESULT CALLBACK IslandWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Windows only knows about the HWND, so we stash a pointer to our C++
    // object on the window when it's created and fetch it for every message.
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<IslandWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (auto* self = reinterpret_cast<IslandWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT IslandWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        // A click must never take focus away from the app you're using.
        return MA_NOACTIVATE;

    case WM_MOUSEMOVE:
        OnMouseMove(point);
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        OnLeftDown(point);
        return 0;

    case WM_LBUTTONUP:
        OnLeftUp(point);
        return 0;

    case WM_RBUTTONUP:
        OnRightUp(point);
        return 0;

    case WM_MOUSEWHEEL:
        OnWheel(point, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_CAPTURECHANGED:
        // Capture ended: normally because we released it, but Windows can also
        // take it away (e.g. a system dialog). Either way the press is over.
        if (View* view = InteractiveView(); view && view->Controls().Pressing())
        {
            view->Controls().CancelPress();
        }
        m_machine.PointerCaptured(false);
        return 0;

    case WM_DISPLAYCHANGE:  // monitor plugged/unplugged, resolution or primary monitor changed
    case WM_DPICHANGED:     // display scale changed, e.g. 125 % -> 175 %
    case WM_SETTINGCHANGE:  // display settings changed
        SchedulePlacement();
        return 0;

    case WM_TIMER:
        switch (wParam)
        {
        case kPlacementTimer:
            KillTimer(hwnd, kPlacementTimer);
            PlaceOnPrimaryMonitor();
            if (m_onDisplaysChanged && message == WM_TIMER)
            {
                m_onDisplaysChanged();
            }
            return 0;
        case kShowTimer:
            m_machine.TimerElapsed(IslandTimer::Show);
            return 0;
        case kHoverTimer:
            m_machine.TimerElapsed(IslandTimer::HoverIntent);
            return 0;
        case kLeaveTimer:
            m_machine.TimerElapsed(IslandTimer::Leave);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        m_hwnd = nullptr;
        break;
    }

    if (message == AppQuitMessage())
    {
        PostQuitMessage(0);
        return 0;
    }
    if (message == AppPoseMessage())
    {
        Pose(static_cast<IslandPose>(wParam));
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
