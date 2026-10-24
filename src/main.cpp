// Island - entry point.
//
// Startup order:
//   1. Make sure only one Island runs. A newer launch replaces the older one,
//      and "Island.exe --quit" just closes the running one.
//   2. Prepare this thread for WinRT and the compositor.
//   3. Start the app: island window, tray icon, watchers (see App.cpp).
//   4. Run the message loop until "Quit Island" is chosen.

#include <windows.h>
#include <Unknwn.h>
#include <DispatcherQueue.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <string_view>

#include "App.h"
#include "AppMessages.h"
#include "IslandWindow.h"
#include "platform/Log.h"

namespace
{
    // A named mutex is a system-wide flag: whoever owns it is "the" running Island.
    constexpr wchar_t kSingleInstanceMutex[] = L"Local\\Island.SingleInstance";

    // Asks the running Island to quit and waits (up to 3 s) until it has.
    // Returns true once this process owns the single-instance mutex.
    bool TakeOverFromRunningCopy(HANDLE mutex)
    {
        if (HWND running = FindWindowW(IslandWindow::kClassName, nullptr))
        {
            PostMessageW(running, AppQuitMessage(), 0, 0);
        }
        const DWORD result = WaitForSingleObject(mutex, 3000);
        return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }

    // The compositor needs a DispatcherQueue (a WinRT work queue) on the UI thread.
    winrt::Windows::System::DispatcherQueueController CreateDispatcherQueueForThisThread()
    {
        DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA };
        winrt::Windows::System::DispatcherQueueController controller{ nullptr };
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));
        return controller;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR commandLine, _In_ int)
{
    const bool quitOnly = std::wstring_view{ commandLine }.find(L"--quit") != std::wstring_view::npos;

    HANDLE rawMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    winrt::handle mutex{ rawMutex };
    if (alreadyRunning && !TakeOverFromRunningCopy(mutex.get()))
    {
        return 1;  // the running copy didn't quit in time
    }
    if (quitOnly)
    {
        return 0;
    }

    try
    {
        Log::Init();
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto dispatcherQueue = CreateDispatcherQueueForThisThread();

        App app;
        if (!app.Start(instance, commandLine))
        {
            return 1;
        }

        // GetMessageW sleeps (0 % CPU) until a message arrives, and returns 0
        // once PostQuitMessage has been called.
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Log::Info(L"quitting");
        return static_cast<int>(msg.wParam);
    }
    catch (const winrt::hresult_error& error)
    {
        MessageBoxW(nullptr, error.message().c_str(), L"Island failed to start", MB_ICONERROR);
        return 1;
    }
}
