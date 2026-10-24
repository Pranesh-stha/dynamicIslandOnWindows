#pragma once

#include <windows.h>

// A message any Island process can send to the running island to make it quit.
// Used when a newer copy starts (it replaces the old one) and by "Island.exe --quit".
inline UINT AppQuitMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Island.Quit");
    return message;
}

// Holds the running island in one state ("pose"), for screenshots and tests
// that mustn't touch your mouse. wParam is an IslandPose value.
inline UINT AppPoseMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Island.Pose");
    return message;
}

enum class IslandPose : WPARAM
{
    None = 0,      // back to normal
    Idle = 1,
    Peek = 2,
    Expanded = 3,
    Demo = 4,      // the control kit demo panel
};
