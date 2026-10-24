#include "services/KeepAwake.h"

#include "platform/Log.h"

KeepAwake::~KeepAwake()
{
    Set(false);
    if (m_request != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_request);
    }
}

bool KeepAwake::Set(bool on)
{
    if (on == m_on)
    {
        return true;
    }
    if (m_request == INVALID_HANDLE_VALUE)
    {
        // The reason Windows shows in `powercfg /requests`.
        REASON_CONTEXT reason{ POWER_REQUEST_CONTEXT_VERSION, POWER_REQUEST_CONTEXT_SIMPLE_STRING };
        reason.Reason.SimpleReasonString = const_cast<wchar_t*>(L"Keep awake (Island)");
        m_request = PowerCreateRequest(&reason);
        if (m_request == INVALID_HANDLE_VALUE)
        {
            Log::Info(L"keep awake: Windows refused ({})", GetLastError());
            return false;
        }
    }
    bool ok;
    if (on)
    {
        ok = PowerSetRequest(m_request, PowerRequestSystemRequired) && PowerSetRequest(m_request, PowerRequestDisplayRequired);
    }
    else
    {
        ok = PowerClearRequest(m_request, PowerRequestSystemRequired) && PowerClearRequest(m_request, PowerRequestDisplayRequired);
    }
    if (ok)
    {
        m_on = on;
        Log::Info(L"keep awake: {}", on ? L"on (no sleep, screen stays on)" : L"off");
    }
    return ok;
}
