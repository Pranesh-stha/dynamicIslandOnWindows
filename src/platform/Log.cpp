#include "platform/Log.h"

#include <windows.h>
#include <ShlObj.h>

#include <cstdio>
#include <share.h>
#include <mutex>

namespace
{
    // Stop writing after this much, so a misbehaving loop can't fill the disk.
    constexpr long kMaxLogBytes = 2 * 1024 * 1024;

    std::mutex g_lock;
    FILE* g_file = nullptr;
    std::wstring g_path;
}

void Log::Init()
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
    {
        return;
    }
    std::wstring folder = std::wstring(localAppData) + L"\\Island";
    CoTaskMemFree(localAppData);
    CreateDirectoryW(folder.c_str(), nullptr);

    std::lock_guard lock(g_lock);
    g_path = folder + L"\\island.log";

    // The last run's log is kept as island.previous.log, so a crash's last
    // lines survive the restart. A run that ended normally logged "quitting".
    const std::wstring previous = folder + L"\\island.previous.log";
    bool endedNormally = true;
    if (MoveFileExW(g_path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        if (FILE* old = _wfsopen(previous.c_str(), L"r, ccs=UTF-8", _SH_DENYNO))
        {
            wchar_t line[1024];
            std::wstring last;
            while (fgetws(line, ARRAYSIZE(line), old))
            {
                last = line;
            }
            fclose(old);
            endedNormally = last.empty() || last.find(L"quitting") != std::wstring::npos;
        }
    }

    // _SH_DENYWR: other programs may read the log while Island runs.
    g_file = _wfsopen(g_path.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
    if (!endedNormally && g_file)
    {
        fwprintf(g_file, L"(the previous run ended unexpectedly; its log is island.previous.log)\n");
        fflush(g_file);
    }
}

std::wstring Log::Path()
{
    std::lock_guard lock(g_lock);
    return g_path;
}

void Log::WriteLine(std::wstring_view line)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring stamped = std::format(L"{:02}:{:02}:{:02}.{:03}  {}\n",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, line);

    OutputDebugStringW(stamped.c_str());  // shows up in the VS Code debug console

    std::lock_guard lock(g_lock);
    if (g_file && ftell(g_file) < kMaxLogBytes)
    {
        fputws(stamped.c_str(), g_file);
        fflush(g_file);
    }
}
