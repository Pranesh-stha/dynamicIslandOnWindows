#include "services/TimerService.h"

#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <format>

#include "platform/Log.h"

int64_t TimerElapsedMs(const TimerState& state, uint64_t now)
{
    int64_t elapsed = state.bankedMs;
    if (state.running && now > state.startedAt)
    {
        elapsed += static_cast<int64_t>(now - state.startedAt);
    }
    return elapsed;
}

int64_t TimerRemainingMs(const TimerState& state, uint64_t now)
{
    return std::max<int64_t>(0, state.durationMs - TimerElapsedMs(state, now));
}

std::wstring TimerText(const TimerState& state, uint64_t now)
{
    int64_t seconds = 0;
    if (state.mode == TimerMode::Countdown)
    {
        seconds = (TimerRemainingMs(state, now) + 999) / 1000;  // up: 4:59.2 left shows 5:00
    }
    else if (state.mode == TimerMode::Stopwatch)
    {
        seconds = TimerElapsedMs(state, now) / 1000;  // down: a whole second must have passed
    }
    const int64_t h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
    return h > 0 ? std::format(L"{}:{:02}:{:02}", h, m, s) : std::format(L"{}:{:02}", m, s);
}

bool TimerHoldsPill(const TimerState& state)
{
    return state.mode != TimerMode::None && (state.running || state.finished);
}

void TimerService::Start(std::function<void(const TimerState&)> onChange)
{
    m_onChange = std::move(onChange);
    const auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_end = queue.CreateTimer();
    m_end.IsRepeating(false);
    m_end.Tick([this](auto&&, auto&&) { Finish(); });
    m_dismiss = queue.CreateTimer();
    m_dismiss.IsRepeating(false);
    m_dismiss.Interval(std::chrono::seconds(30));
    m_dismiss.Tick([this](auto&&, auto&&) { Stop(); });
}

void TimerService::StartCountdown(int minutes)
{
    m_dismiss.Stop();
    m_state = {};
    m_state.mode = TimerMode::Countdown;
    m_state.durationMs = static_cast<int64_t>(std::max(1, minutes)) * 60'000;
    m_state.running = true;
    m_state.startedAt = GetTickCount64();
    Log::Info(L"timer: {} min", minutes);
    Schedule();
    Publish();
}

void TimerService::StartStopwatch()
{
    m_dismiss.Stop();
    m_end.Stop();
    m_state = {};
    m_state.mode = TimerMode::Stopwatch;
    m_state.running = true;
    m_state.startedAt = GetTickCount64();
    Log::Info(L"stopwatch started");
    Publish();
}

void TimerService::PauseResume()
{
    if (m_state.mode == TimerMode::None || m_state.finished)
    {
        return;
    }
    const uint64_t now = GetTickCount64();
    if (m_state.running)
    {
        m_state.bankedMs = TimerElapsedMs(m_state, now);
        m_state.running = false;
        m_end.Stop();
    }
    else
    {
        m_state.running = true;
        m_state.startedAt = now;
        Schedule();
    }
    Publish();
}

void TimerService::AddMinute()
{
    if (m_state.mode != TimerMode::Countdown || m_state.finished)
    {
        return;
    }
    m_state.durationMs += 60'000;
    if (m_state.running)
    {
        Schedule();
    }
    Publish();
}

void TimerService::Stop()
{
    m_end.Stop();
    m_dismiss.Stop();
    if (m_state.mode == TimerMode::None)
    {
        return;
    }
    m_state = {};
    Publish();
}

void TimerService::Schedule()
{
    if (m_state.mode != TimerMode::Countdown || !m_state.running)
    {
        return;
    }
    // One wake-up, at the end. (The island counts the seconds on screen itself.)
    m_end.Stop();
    m_end.Interval(std::chrono::milliseconds(std::max<int64_t>(1, TimerRemainingMs(m_state, GetTickCount64()))));
    m_end.Start();
}

void TimerService::Finish()
{
    m_end.Stop();
    if (m_state.mode != TimerMode::Countdown || !m_state.running)
    {
        return;
    }
    if (TimerRemainingMs(m_state, GetTickCount64()) > 0)
    {
        Schedule();  // woke up a little early
        return;
    }
    m_state.bankedMs = m_state.durationMs;
    m_state.running = false;
    m_state.finished = true;
    Log::Info(L"timer: time's up");
    // Windows' own alarm sound, once.
    if (!PlaySoundW(L"C:\\Windows\\Media\\Alarm01.wav", nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
    {
        MessageBeep(MB_ICONASTERISK);
    }
    m_dismiss.Start();
    Publish();
}

void TimerService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
