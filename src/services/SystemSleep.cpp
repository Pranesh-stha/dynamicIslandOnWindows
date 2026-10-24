#include "services/SystemSleep.h"

#include <windows.h>
#include <powrprof.h>

#include "platform/Log.h"

bool SystemSleep::Now()
{
    // Sleeping needs the "shut down the system" right. Every user has it,
    // but a program has to switch it on for itself first.
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        TOKEN_PRIVILEGES privileges{ 1 };
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid))
        {
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }

    Log::Info(L"going to sleep (locked first)");
    LockWorkStation();
    // Sleep, not hibernate; let wake-up events (keys, the lid) work as usual.
    if (!SetSuspendState(FALSE, FALSE, FALSE))
    {
        Log::Info(L"sleep: Windows refused ({})", GetLastError());
        return false;
    }
    return true;
}
