#include "views/PeekView.h"

#include "platform/Settings.h"
#include "render/Glyphs.h"

namespace
{
    // Two lines of text, centered as a block in the strip.
    void PlaceLines(Label& top, Label& bottom, float x, float height)
    {
        const float textTop = std::round((height - top.Height() - bottom.Height()) / 2.0f);
        top.SetPosition(x, textTop);
        bottom.SetPosition(x, textTop + top.Height());
    }
}

void PeekView::Build()
{
    const Ui& ui = m_ui;
    const LayoutSettings& layout = *ui.layout;
    ResetLayer(ui.EvenPx(layout.peekWidth), ui.Px(layout.peekHeight));
    auto container = [&]
    {
        auto visual = ui.compositor.CreateContainerVisual();
        visual.Size({ m_width, m_height });
        m_layer.Children().InsertAtTop(visual);
        return visual;
    };

    // Nothing playing: a hint.
    m_empty = container();
    const float pad = ui.Px(10.0f);
    const float badge = ui.EvenPx(36.0f);
    const float badgeY = std::round((m_height - badge) / 2.0f);
    m_badge = std::make_unique<RoundedRect>(ui, badge, badge, badge / 2.0f, White(0.14f));
    m_badge->visual.Offset({ pad, badgeY, 0.0f });
    m_empty.Children().InsertAtTop(m_badge->visual);
    m_badgeIcon = std::make_unique<Label>(ui, 16.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_badgeIcon->SetText(std::wstring(1, Glyph::MusicNote));
    m_badgeIcon->SetPosition(pad + std::round((badge - m_badgeIcon->Width()) / 2.0f), badgeY + std::round((badge - m_badgeIcon->Height()) / 2.0f));
    m_empty.Children().InsertAtTop(m_badgeIcon->Visual());
    const float emptyTextX = pad + badge + ui.Px(10.0f);
    m_emptyTitle = std::make_unique<Label>(ui, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_emptyTitle->SetMaxWidth(m_width - emptyTextX - ui.Px(20.0f));
    m_emptyTitle->SetText(L"Nothing playing");
    m_emptySubtitle = std::make_unique<Label>(ui, 11.5f);
    m_emptySubtitle->SetMaxWidth(m_width - emptyTextX - ui.Px(20.0f));
    m_emptySubtitle->SetText(L"Click for the control center");
    m_emptySubtitle->Visual().Opacity(0.6f);
    PlaceLines(*m_emptyTitle, *m_emptySubtitle, emptyTextX, m_height);
    m_empty.Children().InsertAtTop(m_emptyTitle->Visual());
    m_empty.Children().InsertAtTop(m_emptySubtitle->Visual());

    // Playing: artwork, song, play/pause and next.
    m_playing = container();
    const float artInset = std::round((m_height - ui.EvenPx(layout.peekHeight - 16.0f)) / 2.0f);
    m_art = std::make_unique<Artwork>(ui, layout.peekHeight - 16.0f, 10.0f);
    m_art->SetPosition(artInset, artInset);
    m_playing.Children().InsertAtTop(m_art->Visual());

    const float button = ui.EvenPx(34.0f);
    const float buttonY = std::round((m_height - button) / 2.0f);
    const float nextX = m_width - ui.Px(10.0f) - button;
    const float playX = nextX - ui.Px(2.0f) - button;
    m_playPause = std::make_unique<IconButton>(ui, m_media.playing ? Glyph::Pause : Glyph::Play, 34.0f, 16.0f);
    m_playPause->SetBounds(playX, buttonY, button, button);
    m_playPause->onClick = [this] { if (m_actions.playPause) m_actions.playPause(); };
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 34.0f, 14.0f);
    m_next->SetBounds(nextX, buttonY, button, button);
    m_next->onClick = [this] { if (m_actions.next) m_actions.next(); };
    for (IconButton* control : { m_playPause.get(), m_next.get() })
    {
        m_playing.Children().InsertAtTop(control->Visual());
        m_controls.Add(*control);
    }

    const float textX = artInset + m_art->Size() + ui.Px(10.0f);
    const float textWidth = playX - ui.Px(6.0f) - textX;
    m_title = std::make_unique<Label>(ui, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_title->SetMaxWidth(textWidth);
    m_subtitle = std::make_unique<Label>(ui, 11.5f);
    m_subtitle->SetMaxWidth(textWidth);
    m_subtitle->Visual().Opacity(0.6f);
    m_playing.Children().InsertAtTop(m_title->Visual());
    m_playing.Children().InsertAtTop(m_subtitle->Visual());

    Apply(false);
}

void PeekView::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        Apply(true);
    }
}

void PeekView::Apply(bool animate)
{
    const bool has = m_media.hasSession;
    if (animate)
    {
        Motion::EaseTo(m_empty, L"Opacity", has ? 0.0f : 1.0f, 200);
        Motion::EaseTo(m_playing, L"Opacity", has ? 1.0f : 0.0f, 200);
    }
    else
    {
        m_empty.Opacity(has ? 0.0f : 1.0f);
        m_playing.Opacity(has ? 1.0f : 0.0f);
    }
    m_playPause->SetEnabled(has && m_media.canPlayPause);
    m_next->SetEnabled(has && m_media.canNext);
    if (!has)
    {
        return;
    }

    m_title->SetText(m_media.title.empty() ? m_media.app : m_media.title);
    std::wstring subtitle = m_media.artist;
    if (!m_media.title.empty())
    {
        subtitle += subtitle.empty() ? m_media.app : L" · " + m_media.app;
    }
    m_subtitle->SetText(subtitle);
    const float textX = m_art->Visual().Offset().x + m_art->Size() + m_ui.Px(10.0f);
    PlaceLines(*m_title, *m_subtitle, textX, m_height);
    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
    m_playPause->SetGlyph(m_media.playing ? Glyph::Pause : Glyph::Play, /*morph*/ true);
}
