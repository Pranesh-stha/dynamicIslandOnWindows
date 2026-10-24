#pragma once

#include <string>

// "Start with Windows" (tray menu): a shortcut to Island in your Startup
// folder, the standard place for apps that open when you sign in. Windows
// lists it in Task Manager › Startup apps and Settings › Apps › Startup,
// where it can be switched off too. Off until you turn it on.
namespace Startup
{
    bool Enabled();                 // the shortcut is there
    bool SetEnabled(bool enabled);  // adds or removes it; false if that failed

    // What it starts: the installed copy (tools\install.ps1) if there is
    // one, so rebuilding never breaks it (PLAN.md F7); else this exe.
    std::wstring Target();
}
