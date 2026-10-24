#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <winrt/Windows.UI.Composition.h>

// Compositor "effect brushes": image filters the GPU applies live, every
// frame, without our process doing any work.
namespace Effects
{
    // A blurred copy of whatever is behind the window (the "host backdrop"),
    // with its colours boosted by `saturation` (1 = unchanged) and its
    // brightness scaled by `brightness`. Returns the plain blur if the filter
    // isn't available.
    winrt::Windows::UI::Composition::CompositionBrush CreateBackdropBrush(
        const winrt::Windows::UI::Composition::Compositor& compositor, float saturation, float brightness);

    // Clear glass: `source` (a live copy of the screen behind) blurred by
    // `blurPx` (0 = sharp) and then colour-boosted like CreateBackdropBrush.
    winrt::Windows::UI::Composition::CompositionBrush CreateClearGlassBrush(
        const winrt::Windows::UI::Composition::Compositor& compositor,
        const winrt::Windows::UI::Composition::CompositionBrush& source, float blurPx, float saturation, float brightness);

    // Repeats a small image endlessly in both directions (for the grain texture).
    winrt::Windows::UI::Composition::CompositionBrush CreateTiledBrush(
        const winrt::Windows::UI::Composition::Compositor& compositor,
        const winrt::Windows::UI::Composition::ICompositionSurface& tile);
}
