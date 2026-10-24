# Visual check for Island (PLAN.md Phase 3+). Hands-free: it never moves
# your mouse, clicks or takes focus, so you can keep working while it runs.
#
# It shows a test backdrop under the island (white on the left, coloured
# stripes and text on the right) and starts Island with --pose, then switches
# the island between its states with the "Island.Pose" message. For each
# morph it records the pill's width frame by frame (so the spring timing can
# be checked, including a morph reversed halfway) and saves a screenshot.
#
#   pwsh -File tests\visual_check.ps1 [-Config Debug] [-Out <folder>]

param([string]$Config = "Release", [string]$Out = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }
if (-not $Out) { $Out = Join-Path $env:TEMP "island-visual-check" }
New-Item -ItemType Directory -Force $Out | Out-Null

Add-Type -AssemblyName System.Drawing
# Modern .NET spreads System.Drawing over several assemblies; reference the loaded ones.
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread")) -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Threading;

public static class Visual
{
    delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASSEX { public uint cbSize, style; public WndProc proc; public int clsExtra, wndExtra; public IntPtr inst, icon, cursor, brush; public string menu, cls; public IntPtr iconSm; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr h; public uint m; public IntPtr w, l; public uint t; public POINT p; }
    [StructLayout(LayoutKind.Sequential)] struct PAINTSTRUCT { public IntPtr hdc; public int erase; public RECT rc; public int restore, incUpdate; [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] reserved; }

    [DllImport("user32", CharSet = CharSet.Unicode)] static extern ushort RegisterClassEx(ref WNDCLASSEX c);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr p, IntPtr m, IntPtr i, IntPtr param);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr DefWindowProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern uint RegisterWindowMessage(string name);
    [DllImport("user32")] static extern bool GetMessage(out MSG m, IntPtr h, uint a, uint b);
    [DllImport("user32")] static extern bool TranslateMessage(ref MSG m);
    [DllImport("user32")] static extern IntPtr DispatchMessage(ref MSG m);
    [DllImport("user32")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32")] static extern IntPtr BeginPaint(IntPtr h, out PAINTSTRUCT ps);
    [DllImport("user32")] static extern bool EndPaint(IntPtr h, ref PAINTSTRUCT ps);
    [DllImport("user32")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int cx, uint flags);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32")] public static extern int GetSystemMetrics(int i);
    [DllImport("kernel32")] static extern IntPtr GetModuleHandle(string n);

    const uint WS_EX_TOPMOST = 0x8, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
    static WndProc s_proc = Proc;
    static IntPtr s_window;
    public static int Width = 1400, Height = 760;

    static IntPtr Proc(IntPtr h, uint m, IntPtr w, IntPtr l)
    {
        if (m == 0x000F)  // WM_PAINT
        {
            PAINTSTRUCT ps;
            IntPtr dc = BeginPaint(h, out ps);
            using (var g = Graphics.FromHdc(dc))
            {
                g.FillRectangle(Brushes.White, 0, 0, Width / 2, Height);
                Color[] colors = { Color.Red, Color.Orange, Color.Gold, Color.LimeGreen, Color.DeepSkyBlue, Color.RoyalBlue, Color.MediumOrchid };
                for (int x = Width / 2, i = 0; x < Width; x += 40, i++)
                    using (var b = new SolidBrush(colors[i % colors.Length])) g.FillRectangle(b, x, 0, 40, Height);
                using (var font = new Font("Segoe UI", 22))
                    for (int y = 0; y < Height; y += 46)
                    {
                        g.DrawString("The quick brown fox jumps over the lazy dog 0123456789", font, Brushes.Black, 20, y);
                        g.DrawString("Island glass test", font, Brushes.White, Width / 2 + 20, y + 14);
                    }
            }
            EndPaint(h, ref ps);
            return IntPtr.Zero;
        }
        return DefWindowProc(h, m, w, l);
    }

    // The backdrop runs on its own thread with its own message loop. It is
    // always-on-top but never activated, so it doesn't take your focus.
    public static void StartBackdrop()
    {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        var ready = new ManualResetEvent(false);
        var thread = new Thread(() =>
        {
            SetThreadDpiAwarenessContext(new IntPtr(-4));
            var wc = new WNDCLASSEX { cbSize = (uint)Marshal.SizeOf(typeof(WNDCLASSEX)), proc = s_proc, inst = GetModuleHandle(null), cls = "IslandBackdrop" };
            RegisterClassEx(ref wc);
            int x = (GetSystemMetrics(0) - Width) / 2;
            s_window = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "IslandBackdrop", "Island backdrop", 0x80000000, x, 0, Width, Height, IntPtr.Zero, IntPtr.Zero, wc.inst, IntPtr.Zero);
            SetWindowPos(s_window, new IntPtr(-1), 0, 0, 0, 0, 0x1 | 0x2 | 0x10 | 0x40);  // TOPMOST, NOSIZE|NOMOVE|NOACTIVATE|SHOWWINDOW
            ready.Set();
            MSG m;
            while (GetMessage(out m, IntPtr.Zero, 0, 0)) { TranslateMessage(ref m); DispatchMessage(ref m); }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();
        ready.WaitOne();
        Thread.Sleep(300);
    }

    public static void StopBackdrop() { PostMessage(s_window, 0x0010, IntPtr.Zero, IntPtr.Zero); }

    // 1 idle, 2 peek, 3 expanded, 4 control kit demo, 0 release.
    public static void Pose(int pose)
    {
        PostMessage(FindWindow("Island.Pill", null), RegisterWindowMessage("Island.Pose"), new IntPtr(pose), IntPtr.Zero);
    }

    public static RECT IslandRect()
    {
        RECT r; GetWindowRect(FindWindow("Island.Pill", null), out r); return r;
    }

    public static void Shot(string path, int width, int height)
    {
        int x = (GetSystemMetrics(0) - width) / 2;
        using (var bmp = new Bitmap(width, height))
        {
            using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(x, 0, 0, 0, new Size(width, height));
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }

    // Width of the dark glass on row y: walks left from the screen center
    // (over the white half of the backdrop) to the first light pixel. One
    // thin strip of the screen is captured per sample.
    static Bitmap s_strip = new Bitmap(700, 1);
    static Graphics s_stripGraphics = Graphics.FromImage(s_strip);
    public static int PillWidth(int y)
    {
        int cx = GetSystemMetrics(0) / 2;
        s_stripGraphics.CopyFromScreen(cx - 700, y, 0, 0, new Size(700, 1));
        for (int i = 699; i >= 0; i--)
        {
            Color c = s_strip.GetPixel(i, 0);
            if (c.R + c.G + c.B > 3 * 215) return 2 * (699 - i);
        }
        return 1400;
    }

    // Samples the pill's width on row y for `ms` milliseconds; prints each change.
    public static string Timeline(int y, int ms)
    {
        var sb = new System.Text.StringBuilder();
        var watch = Stopwatch.StartNew();
        int last = -1, samples = 0;
        while (watch.ElapsedMilliseconds < ms)
        {
            int w = PillWidth(y);
            samples++;
            if (w != last) { sb.AppendFormat("{0,5} ms  {1} px", watch.ElapsedMilliseconds, w); sb.AppendLine(); last = w; }
        }
        sb.AppendFormat("({0} samples)", samples);
        return sb.ToString();
    }
}
'@

$report = [System.Collections.Generic.List[string]]::new()
function Morph([string]$name, [int]$pose, [int]$row, [int]$ms) {
    [Visual]::Pose($pose)
    $report.Add("--- $name (width on row $row)")
    $report.Add([Visual]::Timeline($row, $ms))
}

[Visual]::StartBackdrop()
Start-Process $exe -ArgumentList "--pose idle"
Start-Sleep -Milliseconds 1500
try {
    $r = [Visual]::IslandRect()
    $report.Add("idle window: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px at ($($r.Left), $($r.Top))")
    # Sample just under the pill's top edge, above any content.
    $row = $r.Bottom - [int](($r.Bottom - $r.Top) * 0.62)
    [Visual]::Shot((Join-Path $Out "1-idle.png"), 700, 110)

    Morph "idle -> peek" 2 $row 1100
    $r = [Visual]::IslandRect(); $report.Add("peek window after settling: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px")
    [Visual]::Shot((Join-Path $Out "2-peek.png"), 700, 110)

    Morph "peek -> expanded" 3 $row 1100
    $r = [Visual]::IslandRect(); $report.Add("expanded window after settling: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px")
    [Visual]::Shot((Join-Path $Out "3-expanded.png"), 900, 600)

    Morph "expanded -> idle" 1 $row 1100
    [Visual]::Shot((Join-Path $Out "4-idle-again.png"), 700, 110)

    # Change of mind halfway: expand, then go back after 120 ms. The width
    # must turn around smoothly, never jump.
    [Visual]::Pose(3)
    Start-Sleep -Milliseconds 120
    Morph "interrupted: expand, back to idle after 120 ms" 1 $row 1100

    [Visual]::Pose(4)
    Start-Sleep -Milliseconds 1200
    [Visual]::Shot((Join-Path $Out "5-demo.png"), 900, 600)
}
finally {
    Start-Process $exe -ArgumentList "--quit" -Wait
    [Visual]::StopBackdrop()
}
$report
"Screenshots: $Out"
