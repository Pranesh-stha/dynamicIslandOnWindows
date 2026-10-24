#include "services/MediaService.h"

#include <shcore.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

#include "platform/Log.h"

namespace wmc = winrt::Windows::Media::Control;
namespace streams = winrt::Windows::Storage::Streams;
using PlaybackStatus = wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

namespace
{
    // Artwork is decoded to this many pixels square: about twice the largest
    // size it's shown at (64 DIP at 175 % = 112 px), and only ~150 KB.
    constexpr UINT kArtworkPixels = 192;

    int64_t NowMs()
    {
        return static_cast<int64_t>(GetTickCount64());
    }

    // Windows sometimes returns empty objects while an app's session starts
    // or ends. Calling anything on an empty object crashes (it isn't an
    // error we can catch), so every object is checked before use.
    bool IsPlaying(const wmc::GlobalSystemMediaTransportControlsSession& session)
    {
        try
        {
            const auto info = session ? session.GetPlaybackInfo() : nullptr;
            return info && info.PlaybackStatus() == PlaybackStatus::Playing;
        }
        catch (const winrt::hresult_error&)
        {
            return false;  // the app went away mid-call
        }
    }

    uint64_t TrackId(const wmc::GlobalSystemMediaTransportControlsSession& session, const std::wstring& title,
                     const std::wstring& artist, const std::wstring& album)
    {
        const size_t hash = std::hash<std::wstring>{}(title + L'\n' + artist + L'\n' + album) ^
                            std::hash<void*>{}(winrt::get_abi(session));
        return hash == 0 ? 1 : hash;
    }
}

MediaService::~MediaService()
{
    m_alive.reset();  // anything still on its way to the UI thread now does nothing
    for (Tracked& tracked : m_sessions)
    {
        Unsubscribe(tracked);
    }
    if (m_manager)
    {
        m_manager.SessionsChanged(m_sessionsToken);
        m_manager.CurrentSessionChanged(m_currentToken);
    }
    if (m_mtaCookie)
    {
        CoDecrementMTAUsage(m_mtaCookie);
    }
}

void MediaService::Start(std::function<void(const MediaState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);

    // Artwork is decoded on thread-pool threads, which need COM's shared
    // "multi-threaded apartment" to exist. This keeps it alive.
    CoIncrementMTAUsage(&m_mtaCookie);

    // After play/pause we show the new state right away (it feels instant);
    // this re-reads the real state in case the app ignored the request.
    m_resync = m_queue.CreateTimer();
    m_resync.Interval(std::chrono::milliseconds(1500));
    m_resync.IsRepeating(false);
    m_resync.Tick([this](auto&&, auto&&) { Update(false); });

    // When an app closes, Windows may still list its session at the moment
    // it says "sessions changed", and no second notice follows. So each
    // notice gets one follow-up look a second later (not a repeating poll).
    m_sessionsFollowUp = m_queue.CreateTimer();
    m_sessionsFollowUp.Interval(std::chrono::milliseconds(1000));
    m_sessionsFollowUp.IsRepeating(false);
    m_sessionsFollowUp.Tick([this](auto&&, auto&&) { SyncSessions(false); });

    StartAsync();
}

winrt::fire_and_forget MediaService::StartAsync()
{
    const std::weak_ptr<bool> alive = m_alive;
    try
    {
        // "co_await" waits without blocking: the UI thread keeps running and
        // this function continues on it once Windows answers.
        auto manager = co_await wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        if (alive.expired())
        {
            co_return;
        }
        m_manager = manager;
        const auto queue = m_queue;
        auto onSessionsChanged = [queue, alive, this](auto&&, auto&&)
        {
            queue.TryEnqueue([alive, this]
            {
                if (!alive.expired())
                {
                    SyncSessions(false);
                    m_sessionsFollowUp.Stop();
                    m_sessionsFollowUp.Start();
                }
            });
        };
        m_sessionsToken = m_manager.SessionsChanged(onSessionsChanged);
        m_currentToken = m_manager.CurrentSessionChanged(onSessionsChanged);
        Log::Info(L"media: watching media sessions");
        SyncSessions(true);
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"media: unavailable ({})", error.message());
    }
}

void MediaService::SyncSessions(bool reloadProperties)
{
    if (!m_manager)
    {
        return;
    }
    try
    {
        const auto sessions = m_manager.GetSessions();
        if (!sessions)
        {
            return;
        }
        auto present = [&](const Session& session)
        {
            for (const auto& candidate : sessions)
            {
                if (candidate == session)
                {
                    return true;
                }
            }
            return false;
        };

        // Forget sessions whose app closed.
        for (Tracked& tracked : m_sessions)
        {
            if (!present(tracked.session))
            {
                Unsubscribe(tracked);
            }
        }
        std::erase_if(m_sessions, [](const Tracked& tracked) { return !tracked.session; });
        if (m_manualChoice && !present(m_manualChoice))
        {
            m_manualChoice = nullptr;
        }

        // Re-read who is playing: a missed event must not leave this stale (E4).
        for (Tracked& tracked : m_sessions)
        {
            const bool playing = IsPlaying(tracked.session);
            if (playing && !tracked.playing)
            {
                tracked.lastActivity = NowMs();
            }
            tracked.playing = playing;
        }

        // Start following new ones.
        for (const auto& session : sessions)
        {
            const bool known = std::any_of(m_sessions.begin(), m_sessions.end(),
                [&](const Tracked& tracked) { return tracked.session == session; });
            if (!known)
            {
                Tracked tracked;
                tracked.session = session;
                tracked.playing = IsPlaying(session);
                tracked.lastActivity = tracked.playing ? NowMs() : 0;
                m_sessions.push_back(tracked);
                Subscribe(m_sessions.back());
            }
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"media: couldn't list sessions ({})", error.message());
    }
    Update(reloadProperties);
}

void MediaService::Subscribe(Tracked& tracked)
{
    // Each app's session reports changes on a background thread; hop to
    // the UI thread, and only if we still exist.
    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    const Session session = tracked.session;
    auto forward = [queue, alive, session, this](Change change)
    {
        return [queue, alive, session, this, change](auto&&, auto&&)
        {
            queue.TryEnqueue([alive, session, this, change]
            {
                if (!alive.expired())
                {
                    OnSessionChanged(session, change);
                }
            });
        };
    };
    try
    {
        tracked.mediaToken = session.MediaPropertiesChanged(forward(Change::Media));
        tracked.playbackToken = session.PlaybackInfoChanged(forward(Change::Playback));
        tracked.timelineToken = session.TimelinePropertiesChanged(forward(Change::Timeline));
    }
    catch (const winrt::hresult_error&)
    {
    }
}

void MediaService::Unsubscribe(Tracked& tracked)
{
    if (!tracked.session)
    {
        return;
    }
    try
    {
        tracked.session.MediaPropertiesChanged(tracked.mediaToken);
        tracked.session.PlaybackInfoChanged(tracked.playbackToken);
        tracked.session.TimelinePropertiesChanged(tracked.timelineToken);
    }
    catch (const winrt::hresult_error&)
    {
    }
    tracked.session = nullptr;
}

void MediaService::OnSessionChanged(const Session& session, Change change)
{
    auto tracked = std::find_if(m_sessions.begin(), m_sessions.end(),
        [&](const Tracked& candidate) { return candidate.session == session; });
    if (tracked == m_sessions.end())
    {
        return;
    }
    if (change == Change::Playback)
    {
        const bool playing = IsPlaying(session);
        if (playing && !tracked->playing)
        {
            // A session that starts playing becomes the one to show, and
            // overrides a manual choice of another app.
            tracked->lastActivity = NowMs();
            if (m_manualChoice && m_manualChoice != session)
            {
                m_manualChoice = nullptr;
            }
        }
        tracked->playing = playing;
    }
    // Only starting to play counts as activity. Browsers send metadata
    // updates all the time, and those mustn't steal the island from the app
    // you most recently pressed play in.
    Update(change == Change::Media && session == m_shown);
}

void MediaService::Update(bool reloadProperties)
{
    // Which session to show.
    std::vector<SessionSummary> summaries;
    int manualIndex = -1;
    int shownIndex = -1;
    Session windowsCurrent{ nullptr };
    try
    {
        windowsCurrent = m_manager ? m_manager.GetCurrentSession() : nullptr;
    }
    catch (const winrt::hresult_error&)
    {
    }
    for (size_t i = 0; i < m_sessions.size(); ++i)
    {
        const Tracked& tracked = m_sessions[i];
        bool sameApp = false;
        try
        {
            sameApp = !m_shownApp.empty() && tracked.session.SourceAppUserModelId() == m_shownApp;
        }
        catch (const winrt::hresult_error&)
        {
        }
        summaries.push_back({ tracked.playing, tracked.lastActivity, windowsCurrent && tracked.session == windowsCurrent, sameApp });
        if (m_manualChoice && tracked.session == m_manualChoice)
        {
            manualIndex = static_cast<int>(i);
        }
        if (m_shown && tracked.session == m_shown)
        {
            shownIndex = static_cast<int>(i);
        }
    }
    const int chosen = PickSession(summaries, manualIndex, shownIndex);
    const Session session = chosen >= 0 ? m_sessions[chosen].session : nullptr;
    const bool switched = session != m_shown;
    m_shown = session;
    if (session)
    {
        try
        {
            m_shownApp = session.SourceAppUserModelId();
        }
        catch (const winrt::hresult_error&)
        {
        }
    }

    if (!session)
    {
        if (m_state.hasSession)
        {
            Log::Info(L"media: nothing playing");
        }
        m_state = MediaState{};
        Publish();
        return;
    }

    try
    {
        m_state.hasSession = true;
        m_state.sessionCount = static_cast<int>(m_sessions.size());
        const std::wstring app = FriendlyAppName(session.SourceAppUserModelId());
        if (switched)
        {
            Log::Info(L"media: showing {}", app);
        }
        m_state.app = app;
        ReadPlayback(session);
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"media: couldn't read the session ({})", error.message());
    }
    if (switched || reloadProperties || m_state.title.empty())
    {
        if (switched)
        {
            // Don't show the previous app's song while the new one loads.
            m_state.title.clear();
            m_state.artist.clear();
            m_state.artwork = nullptr;
            m_state.trackId = 0;
        }
        LoadProperties(session);
    }
    Publish();
}

void MediaService::ReadPlayback(const Session& session)
{
    // Any of these can be empty while the app's session starts or ends (see IsPlaying).
    const auto info = session.GetPlaybackInfo();
    if (!info)
    {
        return;
    }
    m_state.playing = info.PlaybackStatus() == PlaybackStatus::Playing;
    if (const auto controls = info.Controls())
    {
        m_state.canPlayPause = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled();
        m_state.canNext = controls.IsNextEnabled();
        m_state.canPrevious = controls.IsPreviousEnabled();
    }
    const auto rate = info.PlaybackRate();
    m_state.rate = rate ? rate.Value() : 1.0;

    // Where the song is: the position at the moment the app last reported it.
    const auto timeline = session.GetTimelineProperties();
    if (!timeline)
    {
        m_state.hasTimeline = false;
        return;
    }
    using seconds = std::chrono::duration<double>;
    const double start = std::chrono::duration_cast<seconds>(timeline.StartTime()).count();
    const double end = std::chrono::duration_cast<seconds>(timeline.EndTime()).count();
    m_state.durationSeconds = end - start;
    m_state.hasTimeline = m_state.durationSeconds > 0.5;
    m_state.positionSeconds = std::chrono::duration_cast<seconds>(timeline.Position()).count() - start;
    m_state.positionMeasuredAt = timeline.LastUpdatedTime();
}

winrt::fire_and_forget MediaService::LoadProperties(Session session)
{
    const uint64_t generation = ++m_propertiesGeneration;
    const std::weak_ptr<bool> alive = m_alive;
    try
    {
        const auto properties = co_await session.TryGetMediaPropertiesAsync();
        // Only the newest request for the session still shown counts.
        if (alive.expired() || generation != m_propertiesGeneration || session != m_shown || !properties)
        {
            co_return;
        }
        const std::wstring title{ properties.Title() };
        const std::wstring artist{ properties.Artist() };
        const std::wstring album{ properties.AlbumTitle() };
        const uint64_t trackId = TrackId(session, title, artist, album);
        const bool newTrack = trackId != m_state.trackId;
        m_state.title = title;
        m_state.artist = artist;
        if (newTrack)
        {
            m_state.trackId = trackId;
            m_state.artwork = nullptr;  // the placeholder shows until the new artwork is ready
            m_state.artworkTrackId = 0;
        }
        // Some apps send the artwork a moment after the title, in a second update.
        const auto thumbnail = properties.Thumbnail();
        if (newTrack && !thumbnail)
        {
            Log::Info(L"media: the app gave no artwork for this song");
        }
        if (thumbnail && !m_state.artwork && m_artworkLoadingFor != trackId)
        {
            LoadArtwork(thumbnail, trackId);
        }
        Publish();
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"media: couldn't read the song details ({})", error.message());
    }
}

winrt::fire_and_forget MediaService::LoadArtwork(streams::IRandomAccessStreamReference thumbnail, uint64_t trackId)
{
    m_artworkLoadingFor = trackId;
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    winrt::com_ptr<IWICBitmapSource> artwork;
    try
    {
        const auto stream = co_await thumbnail.OpenReadAsync();
        co_await winrt::resume_background();  // decoding is the slow part: not on the UI thread
        winrt::com_ptr<IStream> source;
        winrt::check_hresult(CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), IID_PPV_ARGS(source.put())));
        artwork = DecodeArtwork(source.get(), kArtworkPixels);
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"media: couldn't load the artwork ({})", error.message());
        artwork = nullptr;  // no artwork: the placeholder stays
    }
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    if (m_artworkLoadingFor == trackId)
    {
        m_artworkLoadingFor = 0;
    }
    // The song may have changed while this was loading (PLAN.md E2).
    if (trackId == m_state.trackId && artwork)
    {
        m_state.artwork = artwork;
        m_state.artworkTrackId = trackId;
        Publish();
    }
}

void MediaService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}

void MediaService::PlayPause()
{
    if (!m_shown)
    {
        return;
    }
    // Show the new state at once; the app's confirmation follows.
    m_state.playing = !m_state.playing;
    if (m_state.playing)
    {
        // Measure from now, so the progress bar doesn't jump.
        m_state.positionMeasuredAt = winrt::clock::now();
    }
    Publish();
    try
    {
        m_shown.TryTogglePlayPauseAsync();
    }
    catch (const winrt::hresult_error&)
    {
    }
    m_resync.Stop();
    m_resync.Start();
}

void MediaService::Next()
{
    try
    {
        if (m_shown)
        {
            m_shown.TrySkipNextAsync();
        }
    }
    catch (const winrt::hresult_error&)
    {
    }
}

void MediaService::Previous()
{
    try
    {
        if (m_shown)
        {
            m_shown.TrySkipPreviousAsync();
        }
    }
    catch (const winrt::hresult_error&)
    {
    }
}

void MediaService::ShowNextSession()
{
    if (m_sessions.size() < 2)
    {
        return;
    }
    auto current = std::find_if(m_sessions.begin(), m_sessions.end(),
        [&](const Tracked& tracked) { return tracked.session == m_shown; });
    const size_t next = current == m_sessions.end() ? 0 : (static_cast<size_t>(current - m_sessions.begin()) + 1) % m_sessions.size();
    m_manualChoice = m_sessions[next].session;
    Update(true);
}

void MediaService::Refresh()
{
    SyncSessions(true);
}
