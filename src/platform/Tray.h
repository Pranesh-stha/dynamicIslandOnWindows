#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

// One line in the tray menu. An item without an action is shown grayed out.
struct TrayItem
{
    std::wstring text;
    std::function<void()> onClick;
    bool separator = false;

    static TrayItem Separator() { return { {}, nullptr, true }; }
};

// The icon near the clock, with a menu that includes "Quit Island".
// It has its own hidden window because showing a menu requires briefly
// activating a window, and the island itself must never be activated.
class Tray
{
public:
    Tray() = default;
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;
    ~Tray();

    // buildMenu runs each time the menu opens, so items can reflect the current state.
    bool Create(HINSTANCE instance, std::function<std::vector<TrayItem>()> buildMenu);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void AddIcon();
    void ShowMenu(POINT anchor);

    HWND m_hwnd{};
    NOTIFYICONDATAW m_icon{};
    UINT m_taskbarCreatedMessage{};
    std::function<std::vector<TrayItem>()> m_buildMenu;
};
