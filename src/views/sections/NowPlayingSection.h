#pragma once

#include "services/MediaState.h"
#include "views/sections/Section.h"

// The "Now playing" column: artwork, song, app (click to switch apps when
// several play media), progress line and previous / play-pause / next.
class NowPlayingSection : public Section
{
public:
    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void OnVisibilityChanged(bool visible) override;
    void SetMedia(const MediaState& media, bool animate);

private:
    void Apply(bool animate);
    void UpdateProgress();

    MediaState m_media;
    bool m_visible = false;
    float m_textX = 0.0f, m_textWidth = 0.0f, m_artTop = 0.0f;
    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<Label> m_title, m_artist;
    std::unique_ptr<TextButton> m_app;
    std::unique_ptr<IconButton> m_previous, m_playPause, m_next;
    std::unique_ptr<ProgressBar> m_progress;
};
