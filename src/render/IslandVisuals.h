#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

// The pill's visuals. They live in the system compositor (DWM), not in our
// window's memory: we describe them once and Windows draws them on the GPU,
// including later animations.
class IslandVisuals
{
public:
    void Initialize(HWND hwnd);

    // All values are in physical pixels.
    void Layout(float pillTop, float pillWidth, float pillHeight);

private:
    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_pill{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_shape{ nullptr };
};
