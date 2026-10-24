#include "views/ControlCenter.h"

#include <algorithm>

#include "platform/Settings.h"

void ControlCenter::Build()
{
    const Ui& ui = m_ui;
    // Wide, but never wider than the screen minus a margin on each side.
    float width = ui.EvenPx(ui.layout->panelWidth);
    if (ui.screenWidth > 0.0f)
    {
        width = std::min(width, 2.0f * std::floor((ui.screenWidth - 2.0f * ui.Px(ui.layout->panelMargin)) / 2.0f));
    }
    ResetLayer(width, 0.0f);

    const float pad = ui.Px(24.0f);
    const float inner = width - 2.0f * pad;
    for (Section* section : std::initializer_list<Section*>{ &m_status, &m_nowPlaying, &m_sound, &m_displays, &m_connections })
    {
        section->Reset();
    }
    m_parts.clear();
    for (int i = 0; i < 5; ++i)
    {
        auto part = ui.compositor.CreateContainerVisual();
        part.Size({ width, 1.0f });  // children draw outside it; this is only for the centre point
        part.CenterPoint({ width / 2.0f, 0.0f, 0.0f });
        m_layer.Children().InsertAtTop(part);
        m_parts.push_back(part);
    }

    // The header line, then a hairline under it.
    float y = m_status.Build(m_parts[0], m_controls, pad, pad, inner);
    y += ui.Px(14.0f);
    m_divider = std::make_unique<RoundedRect>(ui, inner, std::max(1.0f, std::round(ui.scale)), 0.0f, White(0.08f));
    m_divider->visual.Offset({ pad, y, 0.0f });
    m_parts[0].Children().InsertAtTop(m_divider->visual);
    y += ui.Px(16.0f);

    // Four columns sharing the width.
    const float gap = ui.Px(28.0f);
    const float available = inner - 3.0f * gap;
    const float nowPlayingWidth = std::floor(available * 0.24f);
    const float soundWidth = std::floor(available * 0.25f);
    const float displaysWidth = std::floor(available * 0.19f);
    const float connectionsWidth = available - nowPlayingWidth - soundWidth - displaysWidth;
    float x = pad;
    float bottom = y;
    bottom = std::max(bottom, m_nowPlaying.Build(m_parts[1], m_controls, x, y, nowPlayingWidth));
    x += nowPlayingWidth + gap;
    bottom = std::max(bottom, m_sound.Build(m_parts[2], m_controls, x, y, soundWidth));
    x += soundWidth + gap;
    bottom = std::max(bottom, m_displays.Build(m_parts[3], m_controls, x, y, displaysWidth));
    x += displaysWidth + gap;
    bottom = std::max(bottom, m_connections.Build(m_parts[4], m_controls, x, y, connectionsWidth));
    SetHeight(std::round(bottom + pad));
}

void ControlCenter::OnVisibilityChanged(bool visible)
{
    for (Section* section : std::initializer_list<Section*>{ &m_status, &m_nowPlaying, &m_sound, &m_displays, &m_connections })
    {
        section->OnVisibilityChanged(visible);
    }
    if (!visible)
    {
        // Nothing animates while hidden: an unfinished entrance jumps to its end.
        for (const auto& part : m_parts)
        {
            part.StopAnimation(L"Opacity");
            part.StopAnimation(L"Offset");
            part.StopAnimation(L"Scale");
            part.Opacity(1.0f);
            part.Offset({ 0.0f, 0.0f, 0.0f });
            part.Scale({ 1.0f, 1.0f, 1.0f });
        }
    }
}

void ControlCenter::OnAppear()
{
    // The header first, then the columns left to right, each sliding down a
    // little into place as it fades in, while the panel itself unfolds.
    const MotionSettings& motion = *m_ui.motion;
    const float drop = m_ui.Px(14.0f);
    for (size_t i = 0; i < m_parts.size(); ++i)
    {
        const auto& part = m_parts[i];
        const int delay = motion.contentFadeInDelayMs + 30 * static_cast<int>(i);
        part.Opacity(0.0f);
        part.Offset({ 0.0f, -drop, 0.0f });
        part.Scale({ 0.985f, 0.985f, 1.0f });
        Motion::EaseTo(part, L"Opacity", 1.0f, motion.contentFadeInMs, delay);
        Motion::SpringTo(part, L"Offset", Motion::float3{ 0.0f, 0.0f, 0.0f }, motion.morph, delay);
        Motion::SpringTo(part, L"Scale", Motion::float3{ 1.0f, 1.0f, 1.0f }, motion.morph, delay);
    }
}
