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
    // _SH_DENYWR: other programs may read the log while Island runs.
    g_file = _wfsopen(g_path.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
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
