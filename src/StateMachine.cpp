#include "StateMachine.h"

void StateMachine::PointerEntered()
{
    m_pointerInside = true;
    m_host.StopTimer(IslandTimer::Leave);
    if (m_state == IslandState::Idle && !m_windowDrag && !m_ignoreHoverUntilLeave)
    {
        m_host.StartTimer(IslandTimer::HoverIntent, m_timing.hoverIntentMs);
    }
}

void StateMachine::PointerLeft()
{
    m_pointerInside = false;
    m_ignoreHoverUntilLeave = false;
    m_host.StopTimer(IslandTimer::HoverIntent);
    if (!m_captured && !m_pinned)
    {
        StartLeaveTimer();
    }
}

void StateMachine::PointerCaptured(bool captured)
{
    m_captured = captured;
    // Letting go of a slider outside the island counts as leaving it.
    if (!captured && !m_pointerInside && !m_pinned)
    {
        StartLeaveTimer();
    }
}

void StateMachine::BackgroundClicked()
{
    if (m_state == IslandState::Idle || m_state == IslandState::Peek)
    {
        m_host.StopTimer(IslandTimer::HoverIntent);
        GoTo(IslandState::Expanded);
    }
}

void StateMachine::WindowDragChanged(bool active)
{
    m_windowDrag = active;
    if (active)
    {
        // Dragging a window to the top of the screen (Snap Layouts) passes
        // over the island. That must not open it.
        m_host.StopTimer(IslandTimer::HoverIntent);
    }
    else if (m_pointerInside)
    {
        // A drag that ended over the island: wait until the mouse leaves.
        m_ignoreHoverUntilLeave = true;
    }
}

void StateMachine::FullscreenChanged(bool fullscreen, bool immediately)
{
    m_fullscreen = fullscreen;
    if (fullscreen)
    {
        m_host.StopTimer(IslandTimer::HoverIntent);
        m_host.StopTimer(IslandTimer::Leave);
        m_host.StopTimer(IslandTimer::Show);
        GoTo(IslandState::Hidden);
    }
    else if (m_state == IslandState::Hidden)
    {
        if (immediately || m_timing.showDelayMs == 0)
        {
            m_host.StopTimer(IslandTimer::Show);
            GoTo(IslandState::Idle);
        }
        else
        {
            m_host.StartTimer(IslandTimer::Show, m_timing.showDelayMs);
        }
    }
}

void StateMachine::OpenExpanded(int leaveDelayMs)
{
    if (m_state == IslandState::Hidden)
    {
        return;
    }
    GoTo(IslandState::Expanded);
    if (!m_pointerInside && !m_pinned)
    {
        m_host.StartTimer(IslandTimer::Leave, leaveDelayMs);
    }
}

void StateMachine::SetPinned(bool pinned)
{
    m_pinned = pinned;
    if (pinned)
    {
        m_host.StopTimer(IslandTimer::Leave);
    }
    else if (!m_pointerInside && !m_captured)
    {
        StartLeaveTimer();
    }
}

void StateMachine::ShowDirectly(IslandState state)
{
    if (m_state != IslandState::Hidden && state != IslandState::Hidden)
    {
        m_host.StopTimer(IslandTimer::HoverIntent);
        GoTo(state);
    }
}

void StateMachine::TimerElapsed(IslandTimer timer)
{
    m_host.StopTimer(timer);
    switch (timer)
    {
    case IslandTimer::HoverIntent:
        // Re-check everything: a timer message may already have been queued
        // when a window drag started.
        if (m_state == IslandState::Idle && m_pointerInside && !m_windowDrag && !m_ignoreHoverUntilLeave)
        {
            GoTo(IslandState::Peek);
        }
        break;

    case IslandTimer::Leave:
        if ((m_state == IslandState::Peek || m_state == IslandState::Expanded) && !m_pointerInside && !m_captured && !m_pinned)
        {
            GoTo(IslandState::Idle);
        }
        break;

    case IslandTimer::Show:
        if (m_state == IslandState::Hidden && !m_fullscreen)
        {
            GoTo(IslandState::Idle);
        }
        break;
    }
}

void StateMachine::StartLeaveTimer()
{
    if (m_state == IslandState::Peek || m_state == IslandState::Expanded)
    {
        m_host.StartTimer(IslandTimer::Leave,
            m_state == IslandState::Expanded ? m_timing.expandedLeaveDelayMs : m_timing.leaveDelayMs);
    }
}

void StateMachine::GoTo(IslandState state)
{
    if (state == m_state)
    {
        return;
    }
    const IslandState from = m_state;
    m_state = state;
    if (state == IslandState::Idle || state == IslandState::Hidden)
    {
        m_host.StopTimer(IslandTimer::Leave);
    }
    if (state == IslandState::Hidden)
    {
        // The window is gone, so the mouse can't be on it any more.
        m_pointerInside = false;
        m_captured = false;
    }
    m_host.EnterState(from, state);
}
