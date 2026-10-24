# Game-safety test for Island (PLAN.md Phase 2).
#
# Creates real test windows (a borderless fullscreen window, a maximized
# window, an "F11" switch, a fullscreen window on the other monitor) and checks
# that the island hides, comes back and goes slim as it should. It also
# measures how fast it hides.
#
# It clicks the test windows to bring them to the front (exactly what you'd do
# with a game), so don't touch the mouse while it runs (about 15 seconds).
#
#   pwsh -File tests\fullscreen_test.ps1            (uses build\Release\Island.exe)
#   pwsh -File tests\fullscreen_test.ps1 -Config Debug

param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }

Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

public static class TestWin
{
    delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASSEX { public uint cbSize, style; public WndProc proc; public int clsExtra, wndExtra; public IntPtr inst, icon, cursor, brush; public string menu, cls; public IntPtr iconSm; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr h; public uint m; public IntPtr w, l; public uint t; public POINT p; }
    [StructLayout(LayoutKind.Sequential)] struct MONITORINFO { public int cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public MOUSEINPUT mi; public uint pad1, pad2; }
    delegate bool MonitorEnumProc(IntPtr m, IntPtr dc, ref RECT r, IntPtr data);

    [DllImport("user32", CharSet = CharSet.Unicode)] static extern ushort RegisterClassEx(ref WNDCLASSEX c);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr DefWindowProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32")] public static extern bool DestroyWindow(IntPtr h);
    [DllImport("user32")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int cx, uint flags);
    [DllImport("user32")] static extern int SetWindowLong(IntPtr h, int index, uint value);
    [DllImport("user32")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] static extern bool PeekMessage(out MSG m, IntPtr h, uint a, uint b, uint remove);
    [DllImport("user32")] static extern bool TranslateMessage(ref MSG m);
    [DllImport("user32")] static extern IntPtr DispatchMessage(ref MSG m);
    [DllImport("user32")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32")] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnumProc proc, IntPtr data);
    [DllImport("user32")] static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO info);
    [DllImport("kernel32")] static extern IntPtr GetModuleHandle(string name);

    const uint WS_POPUP = 0x80000000, WS_VISIBLE = 0x10000000, WS_OVERLAPPEDWINDOW = 0x00CF0000;
    const uint WS_CAPTION = 0x00C00000, WS_THICKFRAME = 0x00040000;
    const uint WS_EX_TOPMOST = 0x8;
    static readonly IntPtr HWND_TOPMOST = new IntPtr(-1), HWND_NOTOPMOST = new IntPtr(-2);
    static WndProc s_proc = (h, m, w, l) => DefWindowProc(h, m, w, l);
    static bool s_registered;

    public static void Init()
    {
        SetThreadDpiAwarenessContext(new IntPtr(-4));  // per-monitor v2: real pixels, like Island
        if (s_registered) return;
        var wc = new WNDCLASSEX { cbSize = (uint)Marshal.SizeOf(typeof(WNDCLASSEX)), proc = s_proc, inst = GetModuleHandle(null), cls = "IslandTestWindow", brush = new IntPtr(2) };
        RegisterClassEx(ref wc);
        s_registered = true;
    }

    public static RECT[] Monitors(out int primaryIndex)
    {
        var list = new System.Collections.Generic.List<RECT>();
        int primary = 0;
        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr m, IntPtr dc, ref RECT r, IntPtr d) =>
        {
            var info = new MONITORINFO { cbSize = Marshal.SizeOf(typeof(MONITORINFO)) };
            GetMonitorInfo(m, ref info);
            if ((info.dwFlags & 1) != 0) primary = list.Count;
            list.Add(info.rcMonitor);
            return true;
        }, IntPtr.Zero);
        primaryIndex = primary;
        return list.ToArray();
    }

    // Created always-on-top so the click is sure to land on it, then made a
    // normal window again (games and videos aren't always-on-top).
    public static IntPtr Create(RECT r, bool borderless)
    {
        uint style = (borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE;
        return CreateWindowEx(WS_EX_TOPMOST, "IslandTestWindow", "Island test window", style, r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top, IntPtr.Zero, IntPtr.Zero, GetModuleHandle(null), IntPtr.Zero);
    }

    public static void ClickToFront(IntPtr window)
    {
        RECT r; GetWindowRect(window, out r);
        SetCursorPos((r.Left + r.Right) / 2, (r.Top + r.Bottom) / 2);
        Pump(30);
        var inputs = new INPUT[2];
        inputs[0].type = 0; inputs[0].mi.flags = 0x2;  // left down
        inputs[1].type = 0; inputs[1].mi.flags = 0x4;  // left up
        SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
        Pump(20);
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, 0x1 | 0x2 | 0x10);  // NOSIZE | NOMOVE | NOACTIVATE
    }

    public static void Maximize(IntPtr window) { ShowWindow(window, 3); }

    // What Chrome does on F11: drop the caption and resize border, cover the monitor.
    public static void GoFullscreen(IntPtr window, RECT monitor)
    {
        SetWindowLong(window, -16, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, IntPtr.Zero, monitor.Left, monitor.Top, monitor.Right - monitor.Left, monitor.Bottom - monitor.Top, 0x20 | 0x4 | 0x10);  // FRAMECHANGED | NOZORDER | NOACTIVATE
    }

    public static void Pump(int ms)
    {
        var watch = Stopwatch.StartNew();
        MSG m;
        while (watch.ElapsedMilliseconds < ms)
        {
            while (PeekMessage(out m, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref m); DispatchMessage(ref m); }
            System.Threading.Thread.Sleep(1);
        }
    }

    // Milliseconds until the island's visibility equals `visible`, or -1 on timeout.
    public static int WaitForIsland(bool visible, int timeoutMs)
    {
        var watch = Stopwatch.StartNew();
        MSG m;
        while (watch.ElapsedMilliseconds < timeoutMs)
        {
            IntPtr island = FindWindow("Island.Pill", null);
            if (island != IntPtr.Zero && IsWindowVisible(island) == visible) return (int)watch.ElapsedMilliseconds;
            while (PeekMessage(out m, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref m); DispatchMessage(ref m); }
            System.Threading.Thread.Sleep(1);
        }
        return -1;
    }

    public static int IslandHeight()
    {
        RECT r; GetWindowRect(FindWindow("Island.Pill", null), out r);
        return r.Bottom - r.Top;
    }
}
'@

$results = [System.Collections.Generic.List[object]]::new()
function Check([string]$name, [bool]$ok, [string]$detail) {
    $results.Add([pscustomobject]@{ Test = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = $detail })
}

[TestWin]::Init()
$cursor = New-Object TestWin+POINT
[void][TestWin]::GetCursorPos([ref]$cursor)
$primaryIndex = 0
$monitors = [TestWin]::Monitors([ref]$primaryIndex)
$primary = $monitors[$primaryIndex]
$small = New-Object TestWin+RECT
$small.Left = $primary.Left + 300; $small.Top = $primary.Top + 300; $small.Right = $small.Left + 800; $small.Bottom = $small.Top + 500

Start-Process $exe
try {
    Check "island appears at start" ([TestWin]::WaitForIsland($true, 5000) -ge 0) ""
    [TestWin]::Pump(300)

    # 1. A borderless window covering the primary monitor = a "windowed fullscreen" game.
    $game = [TestWin]::Create($primary, $true)
    [TestWin]::ClickToFront($game)
    $hideMs = [TestWin]::WaitForIsland($false, 2000)
    Check "hides for a borderless fullscreen window" ($hideMs -ge 0 -and $hideMs -lt 200) "hid after $hideMs ms (limit 200)"

    # 2. Closing it brings the island back, after the show delay.
    [void][TestWin]::DestroyWindow($game)
    $showMs = [TestWin]::WaitForIsland($true, 3000)
    Check "comes back when it closes" ($showMs -ge 300 -and $showMs -lt 1500) "came back after $showMs ms (expected about 500)"

    # 3. A maximized ordinary window: the island stays, as the slim line.
    $normal = [TestWin]::Create($small, $false)
    [TestWin]::ClickToFront($normal)
    [TestWin]::Maximize($normal)
    [TestWin]::Pump(400)
    $visible = [TestWin]::WaitForIsland($true, 100) -ge 0
    Check "stays for a maximized window" $visible ""

    # 4. The same window goes fullscreen like Chrome on F11 (no focus change).
    [TestWin]::GoFullscreen($normal, $primary)
    $hideMs = [TestWin]::WaitForIsland($false, 2000)
    Check "hides when the front window goes fullscreen (F11)" ($hideMs -ge 0 -and $hideMs -lt 200) "hid after $hideMs ms"
    [void][TestWin]::DestroyWindow($normal)
    [void][TestWin]::WaitForIsland($true, 3000)

    if ($monitors.Count -ge 2) {
        $other = $monitors[($primaryIndex + 1) % $monitors.Count]

        # 5. Fullscreen on the other monitor doesn't affect the island.
        $otherGame = [TestWin]::Create($other, $true)
        [TestWin]::ClickToFront($otherGame)
        [TestWin]::Pump(700)
        Check "stays for fullscreen on the other monitor" ([TestWin]::WaitForIsland($true, 100) -ge 0) ""

        # 6. Fullscreen on the primary, then click a window on the other monitor:
        #    the fullscreen window still covers the island's screen, so stay hidden.
        $game = [TestWin]::Create($primary, $true)
        [TestWin]::ClickToFront($game)
        [void][TestWin]::WaitForIsland($false, 1000)
        [TestWin]::ClickToFront($otherGame)
        [TestWin]::Pump(900)
        Check "stays hidden while you use the other monitor" ([TestWin]::WaitForIsland($false, 100) -ge 0) ""
        [void][TestWin]::DestroyWindow($game)
        [void][TestWin]::DestroyWindow($otherGame)
        $showMs = [TestWin]::WaitForIsland($true, 3000)
        Check "comes back after both close" ($showMs -ge 0) "after $showMs ms"
    }
    else {
        Check "other-monitor tests" $true "skipped: only one monitor connected"
    }
}
finally {
    [void][TestWin]::SetCursorPos($cursor.X, $cursor.Y)
    Start-Process $exe -ArgumentList "--quit" -Wait
}

$results | Format-Table -AutoSize | Out-String -Width 200
$log = Join-Path $env:LOCALAPPDATA "Island\island.log"
if (Test-Path $log) { "--- island.log"; Get-Content $log }
if ($results.Result -contains "FAIL") { exit 1 }
