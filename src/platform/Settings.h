#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <winrt/Windows.System.h>

struct GameSettings
{
    // Exe names (lower case) that hide the island whenever they're the app in
    // front, e.g. L"somegame.exe". An escape hatch for anything the automatic
    // fullscreen detection misses.
    std::vector<std::wstring> blocklist;

    // How long nothing may be fullscreen before the island comes back. Hiding
    // is always instant; only coming back waits, so alt-tab doesn't flicker.
    int showDelayMs = 500;
};

struct Settings
{
    GameSettings games;
};

// Reads JSON text into `out`. Keys that are missing keep their defaults, and
// "//" comments are allowed. Returns false on invalid JSON (then `out` holds
// the defaults). `message` explains errors and wrongly typed values.
bool ParseSettings(std::wstring_view text, Settings& out, std::wstring& message);

// settings.json on disk: found next to Island.exe, or (for builds made from
// the repo) in the repo's config folder. Edits are picked up while Island runs.
class SettingsFile
{
public:
    SettingsFile() = default;
    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;
    ~SettingsFile();

    // Reads the file. A missing or broken file means defaults.
    void Load();

    // Calls onChanged on this thread shortly after the file is saved. A save
    // with an error keeps the previous settings (and logs the error).
    void Watch(std::function<void()> onChanged);

    const Settings& Current() const { return m_settings; }
    const std::wstring& Path() const { return m_path; }

private:
    static void CALLBACK OnFolderChanged(void* context, BOOLEAN timedOut);
    bool Read(Settings& out);
    void Reload();

    Settings m_settings;
    std::wstring m_path;
    FILETIME m_lastWrite{};
    HANDLE m_folderChange = INVALID_HANDLE_VALUE;
    HANDLE m_wait = nullptr;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_debounce{ nullptr };
    std::function<void()> m_onChanged;
};
