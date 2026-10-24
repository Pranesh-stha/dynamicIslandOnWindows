#pragma once

#include <memory>

#include "services/MediaState.h"
#include "views/View.h"

// What the small resting pill shows: nothing (plain glass), or while media
// is playing, the artwork on the left and a "playing" mark on the right.
// Everything here is static: an idle pill never animates on its own.
class IdleView : public View
{
public:
    using View::View;
    void Build() override;
    void SetMedia(const MediaState& media);

private:
    void Apply(bool animate);

    MediaState m_media;
    winrt::Windows::UI::Composition::ContainerVisual m_mediaLayer{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_bars{ nullptr };
    std::unique_ptr<Artwork> m_art;
};
