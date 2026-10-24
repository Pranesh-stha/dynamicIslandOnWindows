#pragma once

#include <format>
#include <string>
#include <string_view>

// A small text log at %LOCALAPPDATA%\Island\island.log, rewritten at every start.
// It records events (hidden/shown, settings reloads, service errors), never a
// constant stream, so it costs nothing while Island is idle. Safe to call from
// any thread.
namespace Log
{
    void Init();
    std::wstring Path();
    void WriteLine(std::wstring_view line);

    template <typename... Args>
    void Info(std::wformat_string<Args...> format, Args&&... args)
    {
        WriteLine(std::format(format, std::forward<Args>(args)...));
    }
}
