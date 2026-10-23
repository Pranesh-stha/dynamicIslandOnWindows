#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>

// The icon near the clock; its menu is how you quit Island.
// It has its own hidden window because showing a menu requires briefly
// activating a window, and the island itself must never be activated.
class Tray
{
public:
    Tray() = default;
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;
    ~Tray();

    bool Create(HINSTANCE instance, std::function<void()> onQuit);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void AddIcon();
    void ShowMenu(POINT anchor);

    HWND m_hwnd{};
    NOTIFYICONDATAW m_icon{};
    UINT m_taskbarCreatedMessage{};
    std::function<void()> m_onQuit;
};
