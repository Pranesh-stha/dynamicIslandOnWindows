#pragma once

#include <functional>

// What the views can ask the rest of Island to do. App fills these in, so
// the views never talk to services directly.
struct IslandActions
{
    // Now playing
    std::function<void()> playPause;
    std::function<void()> next;
    std::function<void()> previous;
    std::function<void()> nextMediaSession;  // when several apps play media
};
