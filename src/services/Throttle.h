#pragma once

#include <cstdint>
#include <limits>
#include <optional>

// Decides when to send a value to slow hardware, like a monitor's
// brightness over DDC/CI (PLAN.md E7, E9):
//   - only the newest wanted value matters (older ones are dropped);
//   - at most one write per `intervalMs`, so the monitor's settings memory
//     isn't worn out and slow calls don't pile up;
//   - the last value is always sent eventually, so the final position of a
//     slider is what the hardware ends up with.
// Times are milliseconds from any steady clock. Pure logic, unit-tested.
class Throttle
{
public:
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

    explicit Throttle(int64_t intervalMs) : m_interval(intervalMs) {}

    void Want(int value) { m_wanted = value; }

    // The value to write now, if any. Otherwise `nextCheck` says when to ask
    // again (kNever when there's nothing to send).
    std::optional<int> Take(int64_t now, int64_t& nextCheck)
    {
        nextCheck = kNever;
        if (!m_wanted || (m_written && *m_wanted == *m_written))
        {
            m_wanted.reset();
            return std::nullopt;
        }
        if (m_lastWrite != kNever && now - m_lastWrite < m_interval)
        {
            nextCheck = m_lastWrite + m_interval;
            return std::nullopt;
        }
        m_written = m_wanted;
        m_wanted.reset();
        m_lastWrite = now;
        return m_written;
    }

    // The hardware reported a value (e.g. it was changed elsewhere).
    void Known(int value) { m_written = value; }

    // When the last write happened (kNever if none).
    int64_t LastWrite() const { return m_lastWrite; }
    bool Pending() const { return m_wanted.has_value(); }

private:
    int64_t m_interval;
    std::optional<int> m_wanted;
    std::optional<int> m_written;
    int64_t m_lastWrite = kNever;
};
