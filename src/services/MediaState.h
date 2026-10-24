#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <wincodec.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <winrt/Windows.Foundation.h>

// What's playing, as the island shows it. Filled in by MediaService.
struct MediaState
{
    bool hasSession = false;  // some app is offering media controls
    std::wstring app;         // "Spotify", "Chrome", "Media Player", ...
    std::wstring title;
    std::wstring artist;
    bool playing = false;
    bool canPlayPause = false;
    bool canNext = false;
    bool canPrevious = false;
    int sessionCount = 0;     // more than 1: the app name can be clicked to switch

    // Where the song is. Some apps don't say (live streams, some players):
    // then hasTimeline is false and no progress bar is shown.
    bool hasTimeline = false;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    double rate = 1.0;  // playback speed
    winrt::Windows::Foundation::DateTime positionMeasuredAt{};

    // Changes whenever the song changes, so late artwork can be matched to
    // the right song.
    uint64_t trackId = 0;
    winrt::com_ptr<IWICBitmapSource> artwork;  // small, already decoded; null while loading or if none
    uint64_t artworkTrackId = 0;

    // The position right now, counting the time since the app reported it.
    double PositionAt(winrt::Windows::Foundation::DateTime now) const;
};

// What the picker needs to know about each media session.
struct SessionSummary
{
    bool playing = false;
    int64_t lastActivity = 0;     // when it last started playing
    bool windowsCurrent = false;  // the one Windows itself would show
};

// Which session the island shows (PLAN.md E1), by index:
//   1. your manual choice, while it still exists;
//   2. one that is playing (the most recently active);
//   3. the one already shown, if nothing is playing: paused apps don't
//      take turns as Windows reshuffles them;
//   4. the one Windows itself shows, else the most recently active.
// Returns -1 when there are none. Pass -1 for "no manual choice" / "nothing shown".
int PickSession(const std::vector<SessionSummary>& sessions, int manualChoice, int shown);

// "Spotify.exe" -> "Spotify", "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" -> "Media Player".
std::wstring FriendlyAppName(std::wstring_view appUserModelId);

// Decodes album artwork (any format Windows can read) to `pixels` square:
// scaled so the short side fits, then the middle square is cut out (video
// thumbnails are 16:9). The pixels are kept decoded in memory, ready for
// Direct2D. Slow-ish: call it off the UI thread. Throws on a broken image.
winrt::com_ptr<IWICBitmapSource> DecodeArtwork(IStream* image, UINT pixels);
