#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

#include "render/Motion.h"

class Graphics;
struct GlassSettings;

// The island's outline, in physical pixels.
struct IslandShape
{
    float width = 0.0f;
    float height = 0.0f;
    float top = 0.0f;     // gap between the top of the screen and the island
    float radius = 0.0f;  // corner radius; anything above height/2 means "pill"
    bool operator==(const IslandShape&) const = default;
};

// The pill's visuals. They live in the system compositor (DWM), not in our
// window's memory: we describe them once and Windows draws them on the GPU,
// including every animation.
//
//   root                  fills the window
//   └─ pill               centered at the top; its size is the morphing outline
//        ├─ glass         blurred, colour-boosted copy of what's behind
//        ├─ tint          dark layer for contrast
//        ├─ grain         faint noise, hides banding in the blur
//        ├─ content       what the views show (Idle / Peek / Expanded layers)
//        └─ rim           thin light edge along the outline
//
// The pill is centered relative to the window, so resizing the window (which
// happens at the start of a morph) never moves it, not even for one frame.
class IslandVisuals
{
public:
    void Initialize(HWND hwnd, const winrt::Windows::UI::Composition::Compositor& compositor, Graphics& graphics);

    // Rebuilds the glass material. `effectsEnabled` is false when Windows has
    // transparency effects turned off (then the glass becomes solid dark).
    void ApplyGlass(const GlassSettings& glass, bool effectsEnabled, float scale);

    // Views add their content layers to this container.
    winrt::Windows::UI::Composition::ContainerVisual Content() const { return m_content; }

    // Springs the outline to `shape` (or jumps there when animate is false).
    // onSettled runs when the motion has finished, unless a newer MorphTo
    // replaced it first.
    void MorphTo(const IslandShape& shape, const Spring& spring, bool animate, std::function<void()> onSettled = nullptr);

    // Fades the whole island in or out (out = tucked away above the top edge).
    void SetPresent(bool present, bool animate);

private:
    void FollowPill(const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry& geometry);
    void CreateGrain(Graphics& graphics);

    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_pill{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_content{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_glass{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_tint{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_grain{ nullptr };
    winrt::Windows::UI::Composition::ShapeVisual m_rim{ nullptr };
    winrt::Windows::UI::Composition::CompositionSpriteShape m_rimShape{ nullptr };
    winrt::Windows::UI::Composition::CompositionPropertySet m_properties{ nullptr };
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_grainSurface{ nullptr };
    winrt::Windows::UI::Composition::CompositionScopedBatch m_batch{ nullptr };
    uint64_t m_generation = 0;
};
