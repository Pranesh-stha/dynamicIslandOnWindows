#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <cstdint>
#include <functional>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

// The timer and stopwatch (PLAN.md Phase 8).
enum class TimerMode
{
    None,
    Countdown,
    Stopwatch,
};

struct TimerState
{
    TimerMode mode = TimerMode::None;
    bool running = false;
    bool finished = false;   // a countdown reached 0: "Time's up" until dismissed
    int64_t durationMs = 0;  // countdown: its length
    int64_t bankedMs = 0;    // time counted before the last start or resume
    uint64_t startedAt = 0;  // GetTickCount64() at the last start or resume, while running
};

// Pure rules, unit-tested.
int64_t TimerElapsedMs(const TimerState& state, uint64_t now);
int64_t TimerRemainingMs(const TimerState& state, uint64_t now);  // countdowns; never below 0
// "4:59", "12:05", "1:02:03". A countdown shows the second it's in (5:00
// right after starting 5 minutes); a stopwatch the seconds completed.
std::wstring TimerText(const TimerState& state, uint64_t now);
// While a timer counts or says "Time's up", the pill stays down (no tucking
// away); paused or stopped, the island tucks away as usual.
bool TimerHoldsPill(const TimerState& state);

// Runs the timer: one wake-up at the end of a countdown (and none for a
// stopwatch); the island redraws the time once a second only while it shows.
// At the end it plays Windows' alarm sound once and says "Time's up" for 30 s
// unless dismissed. Results arrive on the UI thread.
class TimerService
{
public:
    void Start(std::function<void(const TimerState&)> onChange);

    void StartCountdown(int minutes);
    void StartStopwatch();
    void PauseResume();
    void AddMinute();  // countdowns
    void Stop();       // no timer any more (also dismisses "Time's up")
    const TimerState& Current() const { return m_state; }

private:
    void Schedule();
    void Finish();
    void Publish();

    std::function<void(const TimerState&)> m_onChange;
    winrt::Windows::System::DispatcherQueueTimer m_end{ nullptr };      // the countdown's end
    winrt::Windows::System::DispatcherQueueTimer m_dismiss{ nullptr };  // "Time's up" goes away by itself
    TimerState m_state;
};
