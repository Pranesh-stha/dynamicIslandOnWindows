#include "platform/Tray.h"

#include <windowsx.h>

#include <utility>

namespace
{
    constexpr wchar_t kClassName[] = L"Island.Tray";
    constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    constexpr UINT kIconId = 1;
    constexpr UINT kCommandQuit = 1;
}

Tray::~Tray()
{
    if (m_hwnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_icon);
        DestroyWindow(m_hwnd);
    }
}

bool Tray::Create(HINSTANCE instance, std::function<void()> onQuit)
{
    m_onQuit = std::move(onQuit);

    // Explorer broadcasts this when it (re)starts; the icon then has to be added again.
    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // A hidden window that is never shown. It isn't a "message-only" window,
    // because those don't receive the TaskbarCreated broadcast.
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Island tray", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        return false;
    }

    m_icon.cbSize = sizeof(m_icon);
    m_icon.hWnd = m_hwnd;
    m_icon.uID = kIconId;
    m_icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_icon.uCallbackMessage = kTrayCallbackMessage;
    m_icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // placeholder until Island has its own icon
    wcscpy_s(m_icon.szTip, L"Island");
    m_icon.uVersion = NOTIFYICON_VERSION_4;
    AddIcon();
    return true;
}

void Tray::AddIcon()
{
    Shell_NotifyIconW(NIM_ADD, &m_icon);
    Shell_NotifyIconW(NIM_SETVERSION, &m_icon);
}

LRESULT CALLBACK Tray::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }

    if (auto* self = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
    {
        if (message == kTrayCallbackMessage)
        {
            // With NOTIFYICON_VERSION_4 the event is in LOWORD(lParam) and the
            // click position is in wParam.
            const UINT event = LOWORD(lParam);
            if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT)
            {
                self->ShowMenu({ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
            }
            return 0;
        }
        if (message == self->m_taskbarCreatedMessage)
        {
            self->AddIcon();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void Tray::ShowMenu(POINT anchor)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Island (Phase 1)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandQuit, L"Quit Island");

    // Needed so the menu closes when you click elsewhere. This activates only
    // the tray's hidden window, and only because you clicked the tray icon.
    SetForegroundWindow(m_hwnd);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align,
        anchor.x, anchor.y, m_hwnd, nullptr));
    DestroyMenu(menu);

    if (command == kCommandQuit && m_onQuit)
    {
        m_onQuit();
    }
}
