#include "views/View.h"

#include <chrono>

namespace
{
    constexpr float kHiddenScale = 0.96f;

    // How long a replaced layer is kept (hidden) before it's released.
    constexpr uint64_t kRetireMs = 3000;
}

View::~View()
{
    if (m_sweep)
    {
        m_sweep.Stop();
    }
    for (const Retired& retired : m_retired)
    {
        m_parent.Children().Remove(retired.layer);
    }
    if (m_layer)
    {
        m_parent.Children().Remove(m_layer);
    }
}

void View::ResetLayer(float width, float height)
{
    m_controls.Clear();
    if (m_layer)
    {
        Retire(m_layer);
    }
    m_width = width;
    m_height = height;

    m_layer = m_ui.compositor.CreateContainerVisual();
    m_layer.Size({ width, height });
    m_layer.AnchorPoint({ 0.5f, 0.0f });                 // hang from the top center of the pill
    m_layer.RelativeOffsetAdjustment({ 0.5f, 0.0f, 0.0f });
    m_layer.CenterPoint({ width / 2.0f, 0.0f, 0.0f });   // scale towards the top center
    m_layer.Opacity(m_visible ? 1.0f : 0.0f);
    const float scale = m_visible ? 1.0f : kHiddenScale;
    m_layer.Scale({ scale, scale, 1.0f });
    m_parent.Children().InsertAtTop(m_layer);
}

void View::Retire(const winrt::Windows::UI::Composition::ContainerVisual& layer)
{
    // The old layer may still have spring animations running. The compositor
    // crashes (a Windows bug, seen in dwmcore.dll) if a spring's target is
    // released before it has read the spring's starting value. So the old
    // layer is only hidden now, and released a few seconds later.
    layer.IsVisible(false);
    m_retired.push_back({ layer, GetTickCount64() });
    if (!m_sweep)
    {
        m_sweep = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_sweep.Interval(std::chrono::milliseconds(kRetireMs));
        m_sweep.IsRepeating(false);
        m_sweep.Tick([this](auto&&, auto&&) { Sweep(); });
    }
    if (!m_sweep.IsRunning())
    {
        m_sweep.Start();
    }
}

void View::Sweep()
{
    const uint64_t now = GetTickCount64();
    std::erase_if(m_retired, [&](const Retired& retired)
    {
        if (now - retired.at < kRetireMs)
        {
            return false;
        }
        m_parent.Children().Remove(retired.layer);
        return true;
    });
    if (!m_retired.empty())
    {
        m_sweep.Start();
    }
}

void View::SetHeight(float height)
{
    m_height = height;
    m_layer.Size({ m_width, height });
}

void View::SetVisible(bool visible, bool animate)
{
    const bool changed = visible != m_visible;
    m_visible = visible;
    if (changed)
    {
        OnVisibilityChanged(visible);
    }
    const MotionSettings& motion = *m_ui.motion;
    const float scale = visible ? 1.0f : kHiddenScale;
    if (!animate)
    {
        m_layer.StopAnimation(L"Opacity");
        m_layer.StopAnimation(L"Scale");
        m_layer.Opacity(visible ? 1.0f : 0.0f);
        m_layer.Scale({ scale, scale, 1.0f });
        return;
    }
    if (visible)
    {
        Motion::EaseTo(m_layer, L"Opacity", 1.0f, motion.contentFadeInMs, motion.contentFadeInDelayMs);
    }
    else
    {
        Motion::EaseTo(m_layer, L"Opacity", 0.0f, motion.contentFadeOutMs);
    }
    Motion::SpringTo(m_layer, L"Scale", Motion::float3{ scale, scale, 1.0f }, motion.morph);
}
