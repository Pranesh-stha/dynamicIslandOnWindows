#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <array>
#include <chrono>
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
//        ├─ glass         frosted: Windows' heavily blurred copy of what's behind
//        ├─ clear         clear glass (when GlassCapture works), over the frosted:
//        │    ├─ view     the live screen behind, lightly blurred, colour-boosted
//        │    └─ lens     a ring along the edge showing what's just beyond it,
//        │                bent inwards and a little brighter, like thick glass
//        ├─ tint          dark layer for contrast
//        ├─ grain         faint noise, hides banding in the blur
//        ├─ sheen         a soft gloss at the top, like light on glass
//        ├─ glow          a soft ring of the album's colour while music plays
//        ├─ content       what the views show (Idle / Peek / Expanded layers)
//        ├─ rim           thin light edge along the outline
//        └─ rim light     a brighter spot on the rim that follows the mouse
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

    // Clear glass: `capture` is a live copy of the top of the screen,
    // `regionWidth` pixels wide and centred on the island (GlassCapture).
    void SetClearSource(const winrt::Windows::UI::Composition::ICompositionSurface& capture, float regionWidth);
    // Shown once the copy has content; the frosted glass shows until then.
    void ShowClear(bool shown, bool animate);

    // The mouse over the island, in the pill's pixels: the rim lights up near it.
    void SetPointer(bool inside, float x, float y);

    // A soft ring of `color` inside the edge (the album's colour while music plays).
    void SetGlow(winrt::Windows::UI::Color color, bool on, bool animate);

private:
    void FollowPill(const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry& geometry);
    void CreateGrain(Graphics& graphics);
    void BuildClear();
    void BuildGlow();
    winrt::Windows::UI::Composition::ExpressionAnimation Follow(const wchar_t* formula) const;
    // A ring along the inside of the outline that fades out inwards, `width`
    // pixels wide, drawn off-screen and kept alive in `visual` and `surface`.
    winrt::Windows::UI::Composition::CompositionSurfaceBrush RingMask(float width,
        winrt::Windows::UI::Composition::ShapeVisual& visual, winrt::Windows::UI::Composition::CompositionVisualSurface& surface);

    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_pill{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_content{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_glass{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_tint{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_grain{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_clearGroup{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_clear{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_lens{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_sheen{ nullptr };
    winrt::Windows::UI::Composition::ShapeVisual m_ring{ nullptr };  // the lens's mask, drawn off-screen
    winrt::Windows::UI::Composition::CompositionVisualSurface m_ringSurface{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_glow{ nullptr };
    winrt::Windows::UI::Composition::CompositionColorBrush m_glowColor{ nullptr };
    winrt::Windows::UI::Composition::ShapeVisual m_glowRing{ nullptr };
    winrt::Windows::UI::Composition::CompositionVisualSurface m_glowSurface{ nullptr };
    winrt::Windows::UI::Composition::ShapeVisual m_rimLight{ nullptr };
    winrt::Windows::UI::Composition::CompositionRadialGradientBrush m_rimLightBrush{ nullptr };
    bool m_pointerInside = false;

    // Where each part of the outline's spring is heading, so that a new
    // morph can start with the old one's speed: width, height, top, radius.
    struct Track
    {
        float from = 0.0f, velocity = 0.0f, to = 0.0f;
    };
    std::array<Track, 4> m_tracks{};
    Spring m_trackSpring{};
    std::chrono::steady_clock::time_point m_trackStart{};
    winrt::Windows::UI::Composition::ICompositionSurface m_clearSource{ nullptr };
    float m_regionWidth = 0.0f;
    bool m_clearStyle = false;
    float m_blur = 0.0f, m_refraction = 0.0f, m_saturation = 1.0f, m_brightness = 1.0f, m_scale = 1.0f;
    winrt::Windows::UI::Composition::ShapeVisual m_rim{ nullptr };
    winrt::Windows::UI::Composition::CompositionSpriteShape m_rimShape{ nullptr };
    winrt::Windows::UI::Composition::CompositionPropertySet m_properties{ nullptr };
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_grainSurface{ nullptr };
    winrt::Windows::UI::Composition::CompositionScopedBatch m_batch{ nullptr };
    uint64_t m_generation = 0;
};
