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

    // The header line, then a hairline under it.
    float y = m_status.Build(m_layer, m_controls, pad, pad, inner);
    y += ui.Px(14.0f);
    m_divider = std::make_unique<RoundedRect>(ui, inner, std::max(1.0f, std::round(ui.scale)), 0.0f, White(0.08f));
    m_divider->visual.Offset({ pad, y, 0.0f });
    m_layer.Children().InsertAtTop(m_divider->visual);
    y += ui.Px(16.0f);

    // Four columns sharing the width.
    const float gap = ui.Px(28.0f);
    const float available = inner - 3.0f * gap;
    const float nowPlayingWidth = std::floor(available * 0.25f);
    const float soundWidth = std::floor(available * 0.26f);
    const float displaysWidth = std::floor(available * 0.16f);
    const float connectionsWidth = available - nowPlayingWidth - soundWidth - displaysWidth;
    float x = pad;
    float bottom = y;
    bottom = std::max(bottom, m_nowPlaying.Build(m_layer, m_controls, x, y, nowPlayingWidth));
    x += nowPlayingWidth + gap;
    bottom = std::max(bottom, m_sound.Build(m_layer, m_controls, x, y, soundWidth));
    x += soundWidth + gap;
    bottom = std::max(bottom, m_displays.Build(m_layer, m_controls, x, y, displaysWidth));
    x += displaysWidth + gap;
    bottom = std::max(bottom, m_connections.Build(m_layer, m_controls, x, y, connectionsWidth));
    SetHeight(std::round(bottom + pad));
}

void ControlCenter::OnVisibilityChanged(bool visible)
{
    for (Section* section : std::initializer_list<Section*>{ &m_status, &m_nowPlaying, &m_sound, &m_displays, &m_connections })
    {
        section->OnVisibilityChanged(visible);
    }
}
