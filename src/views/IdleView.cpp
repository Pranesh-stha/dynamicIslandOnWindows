#include "views/IdleView.h"

#include "platform/Settings.h"

void IdleView::Build()
{
    const LayoutSettings& layout = *m_ui.layout;
    ResetLayer(m_ui.EvenPx(layout.idleWidth), m_ui.Px(layout.idleHeight));

    m_mediaLayer = m_ui.compositor.CreateContainerVisual();
    m_mediaLayer.Size({ m_width, m_height });
    m_mediaLayer.Opacity(0.0f);
    m_layer.Children().InsertAtTop(m_mediaLayer);

    // Artwork on the left, inset evenly from the pill's edge.
    const float inset = m_ui.Px(6.0f);
    m_art = std::make_unique<Artwork>(m_ui, layout.idleHeight - 12.0f, 6.0f);
    m_art->SetPosition(inset + m_ui.Px(1.0f), std::round((m_height - m_art->Size()) / 2.0f));
    m_mediaLayer.Children().InsertAtTop(m_art->Visual());

    // A small, still "sound bars" mark on the right.
    m_bars = m_ui.compositor.CreateContainerVisual();
    const float barWidth = m_ui.Px(3.0f);
    const float gap = m_ui.Px(2.0f);
    const float tallest = m_ui.Px(14.0f);
    const float heights[] = { 0.45f, 0.85f, 0.6f, 1.0f };
    const float barsWidth = 4.0f * barWidth + 3.0f * gap;
    for (int i = 0; i < 4; ++i)
    {
        const float height = std::round(tallest * heights[i]);
        RoundedRect bar(m_ui, barWidth, height, barWidth / 2.0f, White(0.85f));
        bar.visual.Offset({ static_cast<float>(i) * (barWidth + gap), std::round((tallest - height) / 2.0f), 0.0f });
        m_bars.Children().InsertAtTop(bar.visual);
    }
    m_bars.Size({ barsWidth, tallest });
    m_bars.Offset({ m_width - m_ui.Px(14.0f) - barsWidth, std::round((m_height - tallest) / 2.0f), 0.0f });
    m_mediaLayer.Children().InsertAtTop(m_bars);

    Apply(false);
}

void IdleView::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        Apply(Visible());  // hidden content never animates: see View::Retire
    }
}

void IdleView::Apply(bool animate)
{
    const float shown = m_media.hasSession ? 1.0f : 0.0f;
    const float bars = m_media.playing ? 1.0f : 0.35f;  // dimmed while paused
    if (animate)
    {
        Motion::EaseTo(m_mediaLayer, L"Opacity", shown, 220);
        Motion::EaseTo(m_bars, L"Opacity", bars, 220);
    }
    else
    {
        m_mediaLayer.Opacity(shown);
        m_bars.Opacity(bars);
    }
    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
}
