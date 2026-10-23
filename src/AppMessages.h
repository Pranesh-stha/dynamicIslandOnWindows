#pragma once

#include <windows.h>

// A message any Island process can send to the running island to make it quit.
// Used when a newer copy starts (it replaces the old one) and by "Island.exe --quit".
inline UINT AppQuitMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Island.Quit");
    return message;
}
