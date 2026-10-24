#include "platform/Startup.h"

#include <windows.h>
#include <Unknwn.h>
#include <ShlObj.h>

#include <filesystem>

#include <winrt/base.h>

#include "platform/Log.h"

namespace
{
    std::filesystem::path KnownFolder(const KNOWNFOLDERID& id)
    {
        PWSTR path = nullptr;
        std::filesystem::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        {
            result = path;
        }
        CoTaskMemFree(path);
        return result;
    }

    std::filesystem::path ShortcutPath()
    {
        return KnownFolder(FOLDERID_Startup) / L"Island.lnk";
    }
}

std::wstring Startup::Target()
{
    // Where tools\install.ps1 puts Island.
    const auto installed = KnownFolder(FOLDERID_UserProgramFiles) / L"Island" / L"Island.exe";
    std::error_code error;
    if (std::filesystem::exists(installed, error))
    {
        return installed.wstring();
    }
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return self;
}

bool Startup::Enabled()
{
    std::error_code error;
    return std::filesystem::exists(ShortcutPath(), error);
}

bool Startup::SetEnabled(bool enabled)
{
    const auto shortcut = ShortcutPath();
    std::error_code error;
    if (!enabled)
    {
        std::filesystem::remove(shortcut, error);
        Log::Info(L"start with Windows: off");
        return !error;
    }
    try
    {
        // A normal Windows shortcut (.lnk), like the ones installers make.
        const std::wstring target = Target();
        auto link = winrt::create_instance<IShellLinkW>(CLSID_ShellLink);
        winrt::check_hresult(link->SetPath(target.c_str()));
        winrt::check_hresult(link->SetWorkingDirectory(std::filesystem::path(target).parent_path().c_str()));
        winrt::check_hresult(link->SetDescription(L"Island: a Dynamic Island for Windows"));
        winrt::check_hresult(link.as<IPersistFile>()->Save(shortcut.c_str(), TRUE));
        Log::Info(L"start with Windows: on ({})", target);
        return true;
    }
    catch (const winrt::hresult_error& failure)
    {
        Log::Info(L"start with Windows: couldn't add the shortcut ({})", failure.message());
        return false;
    }
}
