#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <memory>

#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "AppMessages.h"
#include "StateMachine.h"
#include "platform/Settings.h"
#include "render/Graphics.h"
#include "render/IslandVisuals.h"
#include "render/Widgets.h"

class View;
class IdleView;
class PeekView;
class ControlKitDemo;

// The island's own window: borderless, transparent, always on top, and it
// never takes focus. It sits at the top-center of the primary monitor and
// follows it when monitors are plugged in, unplugged or rescaled.
//
// It turns mouse input into StateMachine events and shows each state: the
// outline morphs (IslandVisuals) and the matching view fades in.
class IslandWindow : private StateMachine::Host
{
public:
    static constexpr wchar_t kClassName[] = L"Island.Pill";

    IslandWindow();
    IslandWindow(const IslandWindow&) = delete;
    IslandWindow& operator=(const IslandWindow&) = delete;
    ~IslandWindow();

    // Creates the window hidden; SetFullscreenHidden(false) shows it.
    bool Create(HINSTANCE instance);
    void ApplySettings(const Settings& settings);

    // Something fullscreen covers the primary monitor. Hiding is instant;
    // showing waits for the show delay unless `immediately` is set.
    void SetFullscreenHidden(bool hidden, bool immediately = false);

    // A maximized window is under the island: idle becomes a slim line.
    void SetSlim(bool slim);

    // The user is dragging or resizing some window.
    void SetWindowDragActive(bool active);

    // Opens the panel with one of every control (tray menu).
    void OpenControlKitDemo();

    // Holds the island in one state, ignoring the mouse leaving and
    // maximized windows (see AppPoseMessage). IslandPose::None releases it.
    void Pose(IslandPose pose);
    bool Posed() const { return m_posed; }

    HWND Handle() const { return m_hwnd; }
    IslandState State() const { return m_machine.State(); }

private:
    // StateMachine::Host
    void StartTimer(IslandTimer timer, int milliseconds) override;
    void StopTimer(IslandTimer timer) override;
    void EnterState(IslandState from, IslandState to) override;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void OnMouseMove(POINT point);
    void OnMouseLeave();
    void OnLeftDown(POINT point);
    void OnLeftUp(POINT point);
    void OnRightUp(POINT point);
    void OnWheel(POINT screenPoint, int delta);
    void SetPointerInside(bool inside);

    void PlaceOnPrimaryMonitor();
    void SchedulePlacement();
    void Rebuild();
    void ReadSystemColors();
    winrt::Windows::UI::Color PickAccent() const;
    View* ViewFor(IslandState state) const;
    View* ExpandedView() const;
    View* InteractiveView() const;
    IslandShape ShapeFor(IslandState state) const;
    void ShowState(bool animate);
    void ResizeWindow(float width, float height);
    void ShowNow();
    Motion::float2 ToLayer(POINT point, const View& view) const;

    HWND m_hwnd{};
    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    Graphics m_graphics;
    IslandVisuals m_visuals;
    Ui m_ui;
    StateMachine m_machine;

    LayoutSettings m_layout;
    MotionSettings m_motion;
    GlassSettings m_glass;
    int m_showDelayMs = 500;

    std::unique_ptr<IdleView> m_idleView;
    std::unique_ptr<PeekView> m_peekView;
    std::unique_ptr<ControlKitDemo> m_demoView;
    bool m_showDemo = true;  // until Phase 4 there's no real control center yet

    winrt::Windows::UI::ViewManagement::UISettings m_uiSettings{ nullptr };
    winrt::event_token m_effectsChangedToken{};
    bool m_effectsEnabled = true;
    winrt::Windows::UI::Color m_systemAccent{};

    RECT m_monitor{};
    float m_scale = 0.0f;
    SIZE m_windowSize{};
    bool m_slim = false;           // what the island shows: the line or the pill
    bool m_slimRequested = false;  // what the fullscreen watcher asked for
    bool m_posed = false;
    bool m_pointerInside = false;
    bool m_backgroundPress = false;
};
