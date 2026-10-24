#pragma once

#include <memory>

#include "services/MediaState.h"
#include "services/TimerService.h"
#include "views/Actions.h"
#include "views/View.h"

// The wider strip shown while the mouse rests on the island: the song with
// play/pause and next, or a hint when nothing is playing; while a timer or
// stopwatch is on, the time in big digits with pause and stop instead.
class PeekView : public View
{
public:
    PeekView(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)), m_actions(actions) {}

    void Build() override;
    void SetMedia(const MediaState& media);
    void SetTimer(const TimerState& timer, const std::wstring& text);

private:
    void Apply(bool animate);

    const IslandActions& m_actions;
    MediaState m_media;
    TimerState m_timer;
    std::wstring m_timerText;
    winrt::Windows::UI::Composition::ContainerVisual m_empty{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_playing{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_timerLayer{ nullptr };
    std::unique_ptr<RoundedRect> m_timerBadge;
    std::unique_ptr<Label> m_timerIcon, m_timerCaption;
    std::unique_ptr<RollingText> m_timerTime;
    std::unique_ptr<IconButton> m_timerPause, m_timerStop;
    std::unique_ptr<RoundedRect> m_badge;
    std::unique_ptr<Label> m_badgeIcon, m_emptyTitle, m_emptySubtitle, m_title, m_subtitle;
    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<IconButton> m_playPause, m_next;
};
