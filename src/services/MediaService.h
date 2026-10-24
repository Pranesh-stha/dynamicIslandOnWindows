#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>

#include "services/MediaState.h"

// Now playing, from the same place Windows' own media flyout gets it: the
// "system media transport controls" that players report to (Spotify,
// browsers, Media Player...). Event-driven, nothing is polled. Windows
// raises the events on background threads; every result is handed to the
// UI thread before anything else sees it (PLAN.md E5).
class MediaService
{
public:
    MediaService() = default;
    MediaService(const MediaService&) = delete;
    MediaService& operator=(const MediaService&) = delete;
    ~MediaService();

    // onChange runs on this (the UI) thread whenever anything changes.
    void Start(std::function<void(const MediaState&)> onChange);

    void PlayPause();
    void Next();
    void Previous();
    void ShowNextSession();  // the manual switch when several apps play media (E1)

    // Re-reads everything. Windows' media events occasionally stop arriving,
    // so this runs whenever the island opens (E4).
    void Refresh();

    const MediaState& Current() const { return m_state; }

private:
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    enum class Change { Media, Playback, Timeline };

    struct Tracked
    {
        Session session{ nullptr };
        bool playing = false;
        int64_t lastActivity = 0;
        winrt::event_token mediaToken{}, playbackToken{}, timelineToken{};
    };

    winrt::fire_and_forget StartAsync();
    winrt::fire_and_forget LoadProperties(Session session);
    winrt::fire_and_forget LoadArtwork(winrt::Windows::Storage::Streams::IRandomAccessStreamReference thumbnail, uint64_t trackId);
    void SyncSessions(bool reloadProperties);
    void Subscribe(Tracked& tracked);
    static void Unsubscribe(Tracked& tracked);
    void OnSessionChanged(const Session& session, Change change);
    void Update(bool reloadProperties);
    void ReadPlayback(const Session& session);
    void Publish();

    std::function<void(const MediaState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_resync{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_sessionsFollowUp{ nullptr };
    std::shared_ptr<bool> m_alive;  // background callbacks check this before touching us
    CO_MTA_USAGE_COOKIE m_mtaCookie{};

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_manager{ nullptr };
    winrt::event_token m_sessionsToken{}, m_currentToken{};
    std::vector<Tracked> m_sessions;
    Session m_shown{ nullptr };
    Session m_manualChoice{ nullptr };
    uint64_t m_propertiesGeneration = 0;
    uint64_t m_artworkLoadingFor = 0;
    MediaState m_state;
};
