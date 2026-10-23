#include "render/IslandVisuals.h"

#include <windows.ui.composition.interop.h>

namespace wuc = winrt::Windows::UI::Composition;
namespace abi = ABI::Windows::UI::Composition::Desktop;

namespace
{
    // Dark tint laid over the blurred backdrop: {alpha, red, green, blue}.
    // Phase 3 replaces this with the full glass material.
    constexpr winrt::Windows::UI::Color kGlassTint{ 150, 20, 20, 22 };
}

void IslandVisuals::Initialize(HWND hwnd)
{
    m_compositor = wuc::Compositor();

    // Connect the compositor to our window. "true" puts the visuals above any
    // child windows (we have none).
    auto interop = m_compositor.as<abi::ICompositorDesktopInterop>();
    winrt::check_hresult(interop->CreateDesktopWindowTarget(
        hwnd, TRUE, reinterpret_cast<abi::IDesktopWindowTarget**>(winrt::put_abi(m_target))));

    // Layer 1: a blurred copy of whatever is behind the window. Only works
    // because the window sets DWMWA_USE_HOSTBACKDROPBRUSH.
    auto backdrop = m_compositor.CreateSpriteVisual();
    backdrop.Brush(m_compositor.CreateHostBackdropBrush());
    backdrop.RelativeSizeAdjustment({ 1.0f, 1.0f });

    // Layer 2: the dark tint on top.
    auto tint = m_compositor.CreateSpriteVisual();
    tint.Brush(m_compositor.CreateColorBrush(kGlassTint));
    tint.RelativeSizeAdjustment({ 1.0f, 1.0f });

    // Both layers are cut to a rounded-rectangle (pill) shape. The shape is a
    // geometry object, so Phase 3 can animate its size and corner radius.
    m_shape = m_compositor.CreateRoundedRectangleGeometry();
    m_pill = m_compositor.CreateContainerVisual();
    m_pill.Clip(m_compositor.CreateGeometricClip(m_shape));
    m_pill.Children().InsertAtTop(backdrop);
    m_pill.Children().InsertAtTop(tint);

    m_target.Root(m_pill);
}

void IslandVisuals::Layout(float pillTop, float pillWidth, float pillHeight)
{
    m_pill.Offset({ 0.0f, pillTop, 0.0f });
    m_pill.Size({ pillWidth, pillHeight });
    m_shape.Size({ pillWidth, pillHeight });
    m_shape.CornerRadius({ pillHeight / 2.0f, pillHeight / 2.0f });
}
