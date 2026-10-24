#pragma once

#include <memory>

#include "services/MediaState.h"
#include "views/Actions.h"
#include "views/View.h"

// The full panel a click opens: now playing at the top (Phase 4), quick
// controls below (Phase 5).
class ControlCenter : public View
{
public:
    ControlCenter(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)), m_actions(actions) {}

    void Build() override;
    void SetMedia(const MediaState& media);

protected:
    void OnVisibilityChanged(bool visible) override;

private:
    float BuildMedia(float top);
    void ApplyMedia(bool animate);
    void UpdateProgress();

    const IslandActions& m_actions;
    MediaState m_media;
    float m_pad = 0.0f;
    float m_textX = 0.0f, m_textWidth = 0.0f, m_artTop = 0.0f;

    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<Label> m_title, m_artist;
    std::unique_ptr<TextButton> m_app;
    std::unique_ptr<IconButton> m_previous, m_playPause, m_next;
    std::unique_ptr<ProgressBar> m_progress;
};
