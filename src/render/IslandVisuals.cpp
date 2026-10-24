#include "render/IslandVisuals.h"

#include <windows.ui.composition.interop.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "platform/Settings.h"
#include "render/Effects.h"
#include "render/Graphics.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace abi = ABI::Windows::UI::Composition::Desktop;
using winrt::Windows::UI::Color;

namespace
{
    constexpr int kGrainSize = 128;  // pixels; the grain tile repeats

    uint8_t Alpha(float opacity)
    {
        return static_cast<uint8_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    }

    Color White(float opacity)
    {
        return { Alpha(opacity), 255, 255, 255 };
    }
}

void IslandVisuals::Initialize(HWND hwnd, const wuc::Compositor& compositor, Graphics& graphics)
{
    m_compositor = compositor;

    // Connect the compositor to our window. "true" puts the visuals above any
    // child windows (we have none).
    auto interop = m_compositor.as<abi::ICompositorDesktopInterop>();
    winrt::check_hresult(interop->CreateDesktopWindowTarget(
        hwnd, TRUE, reinterpret_cast<abi::IDesktopWindowTarget**>(winrt::put_abi(m_target))));

    m_root = m_compositor.CreateContainerVisual();
    m_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_target.Root(m_root);

    // Centered horizontally relative to the window, hanging from its top.
    m_pill = m_compositor.CreateContainerVisual();
    m_pill.AnchorPoint({ 0.5f, 0.0f });
    m_pill.RelativeOffsetAdjustment({ 0.5f, 0.0f, 0.0f });
    m_root.Children().InsertAtTop(m_pill);

    // The corner radius is its own animated value; the outline uses the
    // smaller of it and half the height, so a short island is always a pill.
    m_properties = m_compositor.CreatePropertySet();
    m_properties.InsertScalar(L"Radius", 0.0f);

    auto clipShape = m_compositor.CreateRoundedRectangleGeometry();
    FollowPill(clipShape);
    m_pill.Clip(m_compositor.CreateGeometricClip(clipShape));

    auto fill = [&](const wuc::Visual& visual)
    {
        visual.RelativeSizeAdjustment({ 1.0f, 1.0f });
        m_pill.Children().InsertAtTop(visual);
    };
    m_glass = m_compositor.CreateSpriteVisual();
    fill(m_glass);
    m_clearGroup = m_compositor.CreateContainerVisual();
    m_clearGroup.Opacity(0.0f);
    fill(m_clearGroup);
    m_clear = m_compositor.CreateSpriteVisual();
    m_clear.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_clearGroup.Children().InsertAtTop(m_clear);
    m_lens = m_compositor.CreateSpriteVisual();
    m_lens.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_clearGroup.Children().InsertAtTop(m_lens);
    m_tint = m_compositor.CreateSpriteVisual();
    fill(m_tint);
    m_grain = m_compositor.CreateSpriteVisual();
    fill(m_grain);
    m_sheen = m_compositor.CreateSpriteVisual();
    fill(m_sheen);
    m_glow = m_compositor.CreateSpriteVisual();
    m_glow.Opacity(0.0f);
    fill(m_glow);
    m_content = m_compositor.CreateContainerVisual();
    fill(m_content);

    // The rim is a stroke along the same outline. The clip cuts off its
    // outer half, leaving a hairline just inside the edge.
    auto rimShape = m_compositor.CreateRoundedRectangleGeometry();
    FollowPill(rimShape);
    m_rimShape = m_compositor.CreateSpriteShape(rimShape);
    m_rim = m_compositor.CreateShapeVisual();
    m_rim.Shapes().Append(m_rimShape);
    fill(m_rim);

    // The rim light: the same outline, stroked with a soft spot of light
    // whose centre the mouse moves.
    auto lightShape = m_compositor.CreateRoundedRectangleGeometry();
    FollowPill(lightShape);
    m_rimLightBrush = m_compositor.CreateRadialGradientBrush();
    m_rimLightBrush.MappingMode(wuc::CompositionMappingMode::Absolute);
    m_rimLightBrush.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0f, White(0.85f)));
    m_rimLightBrush.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0f, White(0.0f)));
    auto light = m_compositor.CreateSpriteShape(lightShape);
    light.StrokeBrush(m_rimLightBrush);
    m_rimLight = m_compositor.CreateShapeVisual();
    m_rimLight.Shapes().Append(light);
    m_rimLight.Opacity(0.0f);
    fill(m_rimLight);

    CreateGrain(graphics);
}

void IslandVisuals::FollowPill(const wuc::CompositionRoundedRectangleGeometry& geometry)
{
    // Expressions are formulas the compositor re-evaluates every frame, so
    // the outline tracks the pill's size even while a spring animates it.
    auto size = m_compositor.CreateExpressionAnimation(L"pill.Size");
    size.SetReferenceParameter(L"pill", m_pill);
    geometry.StartAnimation(L"Size", size);

    auto radius = m_compositor.CreateExpressionAnimation(
        L"Vector2(Min(props.Radius, pill.Size.Y / 2), Min(props.Radius, pill.Size.Y / 2))");
    radius.SetReferenceParameter(L"pill", m_pill);
    radius.SetReferenceParameter(L"props", m_properties);
    geometry.StartAnimation(L"CornerRadius", radius);
}

void IslandVisuals::CreateGrain(Graphics& graphics)
{
    // Random grey pixels from a fixed seed, so the grain looks the same every time.
    std::vector<uint32_t> pixels(kGrainSize * kGrainSize);
    uint32_t seed = 0x1234567u;
    for (uint32_t& pixel : pixels)
    {
        seed = seed * 1664525u + 1013904223u;
        const uint32_t grey = seed >> 24;
        pixel = 0xFF000000u | (grey << 16) | (grey << 8) | grey;
    }

    m_grainSurface = graphics.CreateSurface();
    graphics.Draw(m_grainSurface, kGrainSize, kGrainSize, [&](ID2D1DeviceContext* context)
    {
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(context->CreateBitmap(D2D1::SizeU(kGrainSize, kGrainSize), pixels.data(), kGrainSize * 4, properties, bitmap.put())))
        {
            context->DrawBitmap(bitmap.get());
        }
    });
    m_grain.Brush(Effects::CreateTiledBrush(m_compositor, m_grainSurface));
}

void IslandVisuals::ApplyGlass(const GlassSettings& glass, bool effectsEnabled, float scale)
{
    // Without transparency effects (turned off in Settings, Remote Desktop,
    // maybe Energy Saver) Windows gives no blur, so the glass turns solid.
    if (effectsEnabled)
    {
        m_glass.Brush(Effects::CreateBackdropBrush(m_compositor, glass.saturation, glass.brightness));
    }
    else
    {
        m_glass.Brush(m_compositor.CreateColorBrush({ 255, 30, 30, 32 }));
    }
    m_tint.Brush(m_compositor.CreateColorBrush({ Alpha(glass.tintOpacity), 18, 18, 20 }));

    // Live glass: the window is see-through, so what's behind shows straight
    // through, drawn live by Windows; only part of Windows' blur goes over it.
    const bool live = glass.style == L"live";
    m_glass.Opacity(live && effectsEnabled ? glass.frost : 1.0f);

    // Clearer glass needs less grain (it was there to hide the heavy blur's banding).
    const bool clear = glass.style == L"clear" || live;  // the brighter rim and sheen
    m_grain.Opacity(effectsEnabled ? glass.noiseOpacity * (clear ? 0.4f : 1.0f) : 0.0f);

    // The rim: brightest along the top, like light catching the upper edge
    // of glass, and a fainter glint along the bottom where light leaves it.
    const float rimTop = clear ? 0.50f : 0.30f;
    const float rimBottom = clear ? 0.16f : 0.06f;
    auto rim = m_compositor.CreateLinearGradientBrush();
    rim.StartPoint({ 0.0f, 0.0f });
    rim.EndPoint({ 0.0f, 1.0f });
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0f, White(rimTop * glass.rimOpacity)));
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(0.35f, White(0.10f * glass.rimOpacity)));
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(0.75f, White(0.05f * glass.rimOpacity)));
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0f, White(rimBottom * glass.rimOpacity)));
    m_rimShape.StrokeBrush(rim);
    m_rimShape.StrokeThickness(2.0f * std::max(1.0f, std::round(scale)));
    m_rimLight.Shapes().GetAt(0).as<wuc::CompositionSpriteShape>().StrokeThickness(3.0f * std::max(1.0f, std::round(scale)));
    m_rimLightBrush.EllipseRadius({ 130.0f * scale, 130.0f * scale });

    // The sheen: a soft gloss over the top part, fading out by the middle.
    auto sheen = m_compositor.CreateLinearGradientBrush();
    sheen.StartPoint({ 0.0f, 0.0f });
    sheen.EndPoint({ 0.0f, 1.0f });
    sheen.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0f, White(clear ? 0.10f : 0.06f)));
    sheen.ColorStops().Append(m_compositor.CreateColorGradientStop(0.45f, White(0.015f)));
    sheen.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0f, White(0.0f)));
    m_sheen.Brush(sheen);

    m_clearStyle = glass.style == L"clear" && effectsEnabled;
    m_blur = glass.blur * scale;
    m_refraction = glass.refraction;
    m_saturation = glass.saturation;
    m_brightness = glass.brightness;
    m_scale = scale;
    BuildClear();
    BuildGlow();
}

wuc::ExpressionAnimation IslandVisuals::Follow(const wchar_t* formula) const
{
    auto expression = m_compositor.CreateExpressionAnimation(formula);
    expression.SetReferenceParameter(L"pill", m_pill);
    expression.SetReferenceParameter(L"props", m_properties);
    expression.SetScalarParameter(L"R", m_regionWidth);
    return expression;
}

void IslandVisuals::SetClearSource(const wuc::ICompositionSurface& capture, float regionWidth)
{
    m_clearSource = capture;
    m_regionWidth = regionWidth;
    BuildClear();
}

void IslandVisuals::BuildClear()
{
    if (!m_clearStyle || !m_clearSource)
    {
        m_clear.Brush(nullptr);
        m_lens.Brush(nullptr);
        ShowClear(false, false);
        return;
    }

    // The copy covers a strip centred on the screen (and on the island) from
    // the top edge down, so a point on the pill is at
    //   (R/2 - pill width/2 + x, pill top + y)
    // in the copy. The brush is shifted by the opposite, and follows the
    // pill as it morphs.
    auto view = [&]
    {
        auto brush = m_compositor.CreateSurfaceBrush(m_clearSource);
        brush.Stretch(wuc::CompositionStretch::None);
        brush.HorizontalAlignmentRatio(0.0f);
        brush.VerticalAlignmentRatio(0.0f);
        brush.StartAnimation(L"Offset", Follow(L"Vector2(pill.Size.X / 2 - R / 2, -pill.Offset.Y)"));
        return brush;
    };
    m_clear.Brush(Effects::CreateClearGlassBrush(m_compositor, view(), m_blur, m_saturation, m_brightness));

    if (m_refraction <= 0.0f)
    {
        m_lens.Brush(nullptr);
        return;
    }

    // The lens: the same copy, shrunk a little around the pill's centre, so
    // a point near the edge shows what's D pixels beyond it, as light bends
    // through the rounded edge of thick glass. Seen only through a ring
    // along the edge that fades out inwards.
    const float bend = m_scale * 9.0f * m_refraction;  // D, in pixels
    auto bent = view();
    bent.StartAnimation(L"CenterPoint", Follow(L"Vector2(R / 2, pill.Offset.Y + pill.Size.Y / 2)"));
    auto shrink = Follow(L"Vector2(1 - D * 2 / Max(pill.Size.X, 4 * D), 1 - D * 2 / Max(pill.Size.Y, 4 * D))");
    shrink.SetScalarParameter(L"D", bend);
    bent.StartAnimation(L"Scale", shrink);
    const auto lens = Effects::CreateClearGlassBrush(m_compositor, bent, m_blur * 0.7f, m_saturation * 1.15f, m_brightness * 1.12f);

    const float width = std::max(4.0f, m_scale * 11.0f * std::min(m_refraction, 1.5f));
    auto ring = RingMask(width, m_ring, m_ringSurface);

    auto masked = m_compositor.CreateMaskBrush();
    masked.Source(lens);
    masked.Mask(ring);
    m_lens.Brush(masked);
}

wuc::CompositionSurfaceBrush IslandVisuals::RingMask(float width, wuc::ShapeVisual& visual, wuc::CompositionVisualSurface& surface)
{
    // Strokes along the outline, each a little further in and fainter, drawn
    // off-screen into a surface that follows the pill's size.
    const int steps = 8;
    const float step = width / static_cast<float>(steps);
    visual = m_compositor.CreateShapeVisual();
    visual.StartAnimation(L"Size", Follow(L"pill.Size"));
    for (int i = 0; i < steps; ++i)
    {
        const float inset = step * (static_cast<float>(i) + 0.5f);
        auto geometry = m_compositor.CreateRoundedRectangleGeometry();
        auto at = [&](const wchar_t* formula)
        {
            auto expression = Follow(formula);
            expression.SetScalarParameter(L"I", inset);
            return expression;
        };
        geometry.StartAnimation(L"Offset", at(L"Vector2(I, I)"));
        geometry.StartAnimation(L"Size", at(L"Vector2(Max(pill.Size.X - 2 * I, 0), Max(pill.Size.Y - 2 * I, 0))"));
        geometry.StartAnimation(L"CornerRadius",
            at(L"Vector2(Max(Min(props.Radius, pill.Size.Y / 2) - I, 0), Max(Min(props.Radius, pill.Size.Y / 2) - I, 0))"));
        auto shape = m_compositor.CreateSpriteShape(geometry);
        const float t = static_cast<float>(i) / static_cast<float>(steps - 1);
        shape.StrokeBrush(m_compositor.CreateColorBrush(White((1.0f - t) * (1.0f - t))));
        shape.StrokeThickness(step + 0.5f);  // a little overlap, so no gaps between the strokes
        visual.Shapes().Append(shape);
    }
    surface = m_compositor.CreateVisualSurface();
    surface.SourceVisual(visual);
    surface.StartAnimation(L"SourceSize", Follow(L"pill.Size"));
    auto brush = m_compositor.CreateSurfaceBrush(surface);
    brush.Stretch(wuc::CompositionStretch::None);
    brush.HorizontalAlignmentRatio(0.0f);
    brush.VerticalAlignmentRatio(0.0f);
    return brush;
}

void IslandVisuals::BuildGlow()
{
    // A wide, soft ring of one colour: nothing moves in it, so it costs
    // nothing between changes.
    if (!m_glowColor)
    {
        m_glowColor = m_compositor.CreateColorBrush({ 255, 64, 156, 255 });
    }
    auto masked = m_compositor.CreateMaskBrush();
    masked.Source(m_glowColor);
    masked.Mask(RingMask(m_scale * 16.0f, m_glowRing, m_glowSurface));
    m_glow.Brush(masked);
}

void IslandVisuals::SetGlow(winrt::Windows::UI::Color color, bool on, bool animate)
{
    if (m_glowColor)
    {
        m_glowColor.Color(color);
    }
    const float opacity = on ? 0.35f : 0.0f;
    if (animate)
    {
        Motion::EaseTo(m_glow, L"Opacity", opacity, on ? 500 : 300);
    }
    else
    {
        m_glow.StopAnimation(L"Opacity");
        m_glow.Opacity(opacity);
    }
}

void IslandVisuals::SetPointer(bool inside, float x, float y)
{
    if (inside)
    {
        m_rimLightBrush.EllipseCenter({ x, y });
    }
    if (inside != m_pointerInside)
    {
        m_pointerInside = inside;
        Motion::EaseTo(m_rimLight, L"Opacity", inside ? 1.0f : 0.0f, inside ? 160 : 260);
    }
}

void IslandVisuals::ShowClear(bool shown, bool animate)
{
    const float opacity = shown && m_clearStyle && m_clearSource ? 1.0f : 0.0f;
    if (animate)
    {
        Motion::EaseTo(m_clearGroup, L"Opacity", opacity, 120);
    }
    else
    {
        m_clearGroup.StopAnimation(L"Opacity");
        m_clearGroup.Opacity(opacity);
    }
}

void IslandVisuals::SetPresent(bool present, bool animate)
{
    const float opacity = present ? 1.0f : 0.0f;
    if (animate)
    {
        // Appearing: a quick fade-in while it slides down. Going away: it
        // stays solid while it shrinks and slides up into the top edge, and
        // only fades at the very end (fading first looks like it just vanishes).
        Motion::EaseTo(m_pill, L"Opacity", opacity, present ? 90 : 110, present ? 0 : 190);
    }
    else
    {
        m_pill.StopAnimation(L"Opacity");
        m_pill.Opacity(opacity);
    }
}

void IslandVisuals::MorphTo(const IslandShape& shape, const Spring& spring, bool animate, std::function<void()> onSettled)
{
    const uint64_t generation = ++m_generation;
    const std::array<float, 4> targets{ shape.width, shape.height, shape.top, shape.radius };
    const auto now = std::chrono::steady_clock::now();
    if (!animate)
    {
        for (size_t i = 0; i < targets.size(); ++i)
        {
            m_tracks[i] = { targets[i], 0.0f, targets[i] };
        }
        m_trackStart = now;
        m_pill.StopAnimation(L"Size");
        m_pill.StopAnimation(L"Offset");
        m_properties.StopAnimation(L"Radius");
        m_pill.Size({ shape.width, shape.height });
        m_pill.Offset({ 0.0f, shape.top, 0.0f });
        m_properties.InsertScalar(L"Radius", shape.radius);
        if (onSettled)
        {
            onSettled();
        }
        return;
    }

    // A "scoped batch" tells us when every animation started inside it has finished.
    // Where the outline is and how fast it moves right now (a change of mind
    // halfway): the new spring sets off with that speed instead of from rest.
    const float elapsed = std::chrono::duration<float>(now - m_trackStart).count();
    std::array<float, 4> velocity{};
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const auto point = Motion::SpringAt(m_trackSpring, m_tracks[i].from, m_tracks[i].velocity, m_tracks[i].to, elapsed);
        velocity[i] = point.velocity;
        m_tracks[i] = { point.value, point.velocity, targets[i] };
    }
    m_trackSpring = spring;
    m_trackStart = now;

    m_batch = m_compositor.CreateScopedBatch(wuc::CompositionBatchTypes::Animation);
    Motion::SpringTo(m_pill, L"Size", Motion::float2{ shape.width, shape.height }, Motion::float2{ velocity[0], velocity[1] }, spring);
    Motion::SpringTo(m_pill, L"Offset", Motion::float3{ 0.0f, shape.top, 0.0f }, Motion::float3{ 0.0f, velocity[2], 0.0f }, spring);
    Motion::SpringTo(m_properties, L"Radius", shape.radius, velocity[3], spring);
    m_batch.End();
    m_batch.Completed([this, generation, onSettled](auto&&, auto&&)
    {
        if (generation == m_generation && onSettled)
        {
            onSettled();
        }
    });
}
