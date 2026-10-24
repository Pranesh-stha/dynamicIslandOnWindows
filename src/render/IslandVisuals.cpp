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
    m_tint = m_compositor.CreateSpriteVisual();
    fill(m_tint);
    m_grain = m_compositor.CreateSpriteVisual();
    fill(m_grain);
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
    m_grain.Opacity(effectsEnabled ? glass.noiseOpacity : 0.0f);

    // Brighter at the top, like light catching the upper edge of glass.
    auto rim = m_compositor.CreateLinearGradientBrush();
    rim.StartPoint({ 0.0f, 0.0f });
    rim.EndPoint({ 0.0f, 1.0f });
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0f, White(0.30f * glass.rimOpacity)));
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(0.5f, White(0.10f * glass.rimOpacity)));
    rim.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0f, White(0.06f * glass.rimOpacity)));
    m_rimShape.StrokeBrush(rim);
    m_rimShape.StrokeThickness(2.0f * std::max(1.0f, std::round(scale)));
}

void IslandVisuals::SetPresent(bool present, bool animate)
{
    const float opacity = present ? 1.0f : 0.0f;
    if (animate)
    {
        // Quick to appear, a little softer going away.
        Motion::EaseTo(m_pill, L"Opacity", opacity, present ? 90 : 160);
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
    if (!animate)
    {
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
    m_batch = m_compositor.CreateScopedBatch(wuc::CompositionBatchTypes::Animation);
    Motion::SpringTo(m_pill, L"Size", Motion::float2{ shape.width, shape.height }, spring);
    Motion::SpringTo(m_pill, L"Offset", Motion::float3{ 0.0f, shape.top, 0.0f }, spring);
    Motion::SpringTo(m_properties, L"Radius", shape.radius, spring);
    m_batch.End();
    m_batch.Completed([this, generation, onSettled](auto&&, auto&&)
    {
        if (generation == m_generation && onSettled)
        {
            onSettled();
        }
    });
}
