#include "views/View.h"

namespace
{
    constexpr float kHiddenScale = 0.96f;
}

View::~View()
{
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
        m_parent.Children().Remove(m_layer);
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

void View::SetHeight(float height)
{
    m_height = height;
    m_layer.Size({ m_width, height });
}

void View::SetVisible(bool visible, bool animate)
{
    m_visible = visible;
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
