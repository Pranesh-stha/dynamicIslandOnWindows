#pragma once

#include <memory>

#include "services/MediaState.h"
#include "views/Actions.h"
#include "views/View.h"

// The wider strip shown while the mouse rests on the island: the song with
// play/pause and next, or a hint when nothing is playing.
class PeekView : public View
{
public:
    PeekView(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)), m_actions(actions) {}

    void Build() override;
    void SetMedia(const MediaState& media);

private:
    void Apply(bool animate);

    const IslandActions& m_actions;
    MediaState m_media;
    winrt::Windows::UI::Composition::ContainerVisual m_empty{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_playing{ nullptr };
    std::unique_ptr<RoundedRect> m_badge;
    std::unique_ptr<Label> m_badgeIcon, m_emptyTitle, m_emptySubtitle, m_title, m_subtitle;
    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<IconButton> m_playPause, m_next;
};
