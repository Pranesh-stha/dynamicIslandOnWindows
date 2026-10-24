#include "views/ControlCenter.h"

#include "platform/Settings.h"
#include "render/Glyphs.h"

void ControlCenter::Build()
{
    const float width = m_ui.EvenPx(m_ui.layout->panelWidth);
    ResetLayer(width, 0.0f);
    m_pad = m_ui.Px(18.0f);

    float y = m_pad;
    y = BuildMedia(y);
    SetHeight(y + m_pad);
}

float ControlCenter::BuildMedia(float top)
{
    const Ui& ui = m_ui;
    auto add = [&](Control& control)
    {
        m_layer.Children().InsertAtTop(control.Visual());
        m_controls.Add(control);
    };

    m_artTop = top;
    m_art = std::make_unique<Artwork>(ui, 64.0f, 14.0f);
    m_art->SetPosition(m_pad, top);
    m_layer.Children().InsertAtTop(m_art->Visual());
    const float art = m_art->Size();

    // Previous, play/pause (a little bigger) and next, on the right.
    const float sideButton = ui.EvenPx(36.0f);
    const float playButton = ui.EvenPx(42.0f);
    const float gap = ui.Px(2.0f);
    const float right = m_width - m_pad + ui.Px(4.0f);  // the highlight circles may reach into the margin
    const float nextX = right - sideButton;
    const float playX = nextX - gap - playButton;
    const float previousX = playX - gap - sideButton;
    const float middle = top + art / 2.0f;
    m_previous = std::make_unique<IconButton>(ui, Glyph::Previous, 36.0f, 15.0f);
    m_playPause = std::make_unique<IconButton>(ui, m_media.playing ? Glyph::Pause : Glyph::Play, 42.0f, 20.0f);
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 36.0f, 15.0f);
    m_previous->SetBounds(previousX, std::round(middle - sideButton / 2.0f), sideButton, sideButton);
    m_playPause->SetBounds(playX, std::round(middle - playButton / 2.0f), playButton, playButton);
    m_next->SetBounds(nextX, std::round(middle - sideButton / 2.0f), sideButton, sideButton);
    m_previous->onClick = [this] { if (m_actions.previous) m_actions.previous(); };
    m_playPause->onClick = [this] { if (m_actions.playPause) m_actions.playPause(); };
    m_next->onClick = [this] { if (m_actions.next) m_actions.next(); };
    add(*m_previous);
    add(*m_playPause);
    add(*m_next);

    // Title, artist and the app it's playing in.
    m_textX = m_pad + art + ui.Px(14.0f);
    m_textWidth = previousX - ui.Px(6.0f) - m_textX;
    m_title = std::make_unique<Label>(ui, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_artist = std::make_unique<Label>(ui, 12.5f);
    m_artist->Visual().Opacity(0.65f);
    m_title->SetMaxWidth(m_textWidth);
    m_artist->SetMaxWidth(m_textWidth);
    m_layer.Children().InsertAtTop(m_title->Visual());
    m_layer.Children().InsertAtTop(m_artist->Visual());
    m_app = std::make_unique<TextButton>(ui, 11.0f);
    m_app->SetTextOpacity(0.5f);
    m_app->onClick = [this] { if (m_actions.nextMediaSession) m_actions.nextMediaSession(); };
    add(*m_app);

    // The progress line under the row.
    const float progressY = top + art + ui.Px(14.0f);
    m_progress = std::make_unique<ProgressBar>(ui);
    m_progress->SetBounds(m_pad, progressY, m_width - 2.0f * m_pad, ui.Px(4.0f));
    m_layer.Children().InsertAtTop(m_progress->Visual());

    ApplyMedia(false);
    return progressY + ui.Px(4.0f);
}

void ControlCenter::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        ApplyMedia(true);
    }
}

void ControlCenter::ApplyMedia(bool animate)
{
    const bool has = m_media.hasSession;
    m_title->SetText(!has ? L"Nothing playing" : m_media.title.empty() ? m_media.app : m_media.title);
    m_artist->SetText(!has ? L"Play music or a video and it shows up here" : m_media.artist);

    // The app name switches apps when several are playing media (PLAN.md E1).
    const bool several = m_media.sessionCount > 1;
    m_app->SetText(has ? (several ? m_media.app + L"  \x203A" : m_media.app) : L"", m_textWidth + m_ui.Px(12.0f));
    m_app->SetInteractive(several);

    // Stack the text lines, centered next to the artwork.
    const float appHeight = has ? m_app->Height() : 0.0f;
    const float block = m_title->Height() + m_artist->Height() + appHeight;
    const float textTop = std::round(m_artTop + (m_art->Size() - block) / 2.0f);
    m_title->SetPosition(m_textX, textTop);
    m_artist->SetPosition(m_textX, textTop + m_title->Height());
    m_app->SetBounds(m_textX - m_ui.Px(6.0f), textTop + m_title->Height() + m_artist->Height(), m_app->Width(), m_app->Height());

    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
    m_playPause->SetGlyph(m_media.playing ? Glyph::Pause : Glyph::Play, animate);
    m_previous->SetEnabled(has && m_media.canPrevious);
    m_playPause->SetEnabled(has && m_media.canPlayPause);
    m_next->SetEnabled(has && m_media.canNext);
    UpdateProgress();
}

void ControlCenter::UpdateProgress()
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
    m_progress->Show(position / m_media.durationSeconds, m_media.playing && Visible(), left);
}

void ControlCenter::OnVisibilityChanged(bool visible)
{
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
