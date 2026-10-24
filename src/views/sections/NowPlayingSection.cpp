#include "views/sections/NowPlayingSection.h"

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

float NowPlayingSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    const Ui& ui = m_ui;
    y = AddTitle(layer, L"Now playing", x, y);

    // Artwork, with the song next to it.
    m_artTop = y;
    m_art = std::make_unique<Artwork>(ui, 104.0f, 18.0f);
    m_art->SetPosition(x, y);
    layer.Children().InsertAtTop(m_art->Visual());
    const float art = m_art->Size();
    m_textX = x + art + ui.Px(14.0f);
    m_textWidth = x + width - m_textX;
    m_title = std::make_unique<Label>(ui, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_artist = std::make_unique<Label>(ui, 12.5f);
    m_artist->Visual().Opacity(0.65f);
    m_title->SetMaxWidth(m_textWidth);
    m_title->SetMaxLines(2);  // long video titles
    m_artist->SetMaxWidth(m_textWidth);
    layer.Children().InsertAtTop(m_title->Visual());
    layer.Children().InsertAtTop(m_artist->Visual());
    m_app = std::make_unique<TextButton>(ui, 11.0f);
    m_app->SetTextOpacity(0.5f);
    m_app->onClick = [this] { if (m_actions.nextMediaSession) m_actions.nextMediaSession(); };
    Add(layer, controls, *m_app);

    // The progress line, then the buttons centered under it.
    const float progressY = y + art + ui.Px(16.0f);
    m_progress = std::make_unique<ProgressBar>(ui);
    m_progress->SetBounds(x, progressY, width, ui.Px(4.0f));
    layer.Children().InsertAtTop(m_progress->Visual());

    const float side = ui.EvenPx(38.0f);
    const float play = ui.EvenPx(46.0f);
    const float gap = ui.Px(12.0f);
    const float buttonsY = progressY + ui.Px(4.0f) + ui.Px(12.0f);
    const float middleY = buttonsY + play / 2.0f;
    const float left = x + std::round((width - 2.0f * side - play - 2.0f * gap) / 2.0f);
    m_previous = std::make_unique<IconButton>(ui, Glyph::Previous, 38.0f, 15.0f);
    m_playPause = std::make_unique<IconButton>(ui, m_media.playing ? Glyph::Pause : Glyph::Play, 46.0f, 20.0f);
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 38.0f, 15.0f);
    m_previous->SetBounds(left, std::round(middleY - side / 2.0f), side, side);
    m_playPause->SetBounds(left + side + gap, buttonsY, play, play);
    m_next->SetBounds(left + side + gap + play + gap, std::round(middleY - side / 2.0f), side, side);
    m_previous->onClick = [this] { if (m_actions.previous) m_actions.previous(); };
    m_playPause->onClick = [this] { if (m_actions.playPause) m_actions.playPause(); };
    m_next->onClick = [this] { if (m_actions.next) m_actions.next(); };
    Add(layer, controls, *m_previous);
    Add(layer, controls, *m_playPause);
    Add(layer, controls, *m_next);

    Apply(false);
    return buttonsY + play;
}

void NowPlayingSection::SetMedia(const MediaState& media, bool animate)
{
    m_media = media;
    if (m_art)
    {
        Apply(animate);
    }
}

void NowPlayingSection::Apply(bool animate)
{
    const bool has = m_media.hasSession;
    m_title->SetText(!has ? L"Nothing playing" : m_media.title.empty() ? m_media.app : m_media.title);
    m_artist->SetText(!has ? L"Play music or a video" : m_media.artist);

    // The app name switches apps when several are playing media (PLAN.md E1).
    const bool several = m_media.sessionCount > 1;
    m_app->SetText(has ? (several ? m_media.app + L"  \x203A" : m_media.app) : L"", m_textWidth + m_ui.Px(12.0f));
    m_app->SetInteractive(several);

    // The text lines, centered as a block next to the artwork.
    const float appHeight = has ? m_app->Height() : 0.0f;
    const float block = m_title->Height() + m_artist->Height() + appHeight;
    const float textTop = std::round(m_artTop + (m_art->Size() - block) / 2.0f);
    m_title->SetPosition(m_textX, textTop);
    m_artist->SetPosition(m_textX, textTop + m_title->Height());
    m_app->SetBounds(m_textX - m_ui.Px(6.0f), textTop + m_title->Height() + m_artist->Height(), m_app->Width(), m_app->Height());

    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
    m_playPause->SetGlyph(m_media.playing ? Glyph::Pause : Glyph::Play, /*morph*/ true, animate);
    m_previous->SetEnabled(has && m_media.canPrevious);
    m_playPause->SetEnabled(has && m_media.canPlayPause);
    m_next->SetEnabled(has && m_media.canNext);
    UpdateProgress();
}

void NowPlayingSection::UpdateProgress()
{
    // Hidden when the app doesn't say where the song is (PLAN.md E3).
    const bool show = m_media.hasSession && m_media.hasTimeline;
    m_progress->Visual().Opacity(show ? 1.0f : 0.0f);
    if (!show)
    {
        m_progress->Freeze();
        return;
    }
    const double position = m_media.PositionAt(winrt::clock::now());
    const double left = (m_media.durationSeconds - position) / std::max(0.01, m_media.rate);
    // It only glides while the panel can be seen: no frames drawn while idle.
    m_progress->Show(position / m_media.durationSeconds, m_media.playing && m_visible, left);
}

void NowPlayingSection::OnVisibilityChanged(bool visible)
{
    m_visible = visible;
    if (!m_progress)
    {
        return;
    }
    if (visible)
    {
        UpdateProgress();
    }
    else
    {
        m_progress->Freeze();
    }
}
