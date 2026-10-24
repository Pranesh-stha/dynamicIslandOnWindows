#pragma once

#include <windows.h>

// "Keep awake" (like PowerToys Awake): while on, the PC doesn't go to sleep
// and the screen doesn't turn off. It's a power request, the documented way
// video players and presentation apps do this; `powercfg /requests` lists it
// as Island's. It ends when switched off, and when Island quits.
class KeepAwake
{
public:
    KeepAwake() = default;
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
    ~KeepAwake();

    bool Set(bool on);  // false if Windows refused
    bool On() const { return m_on; }

private:
    HANDLE m_request = INVALID_HANDLE_VALUE;
    bool m_on = false;
};
