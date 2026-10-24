#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <d3d11.h>

#include <cstdint>
#include <functional>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.UI.Composition.h>

class Graphics;

// What's on screen behind the island, for the clear glass (PLAN.md §5,
// "Clear glass"). Windows' own backdrop is already heavily blurred and can't
// be made clearer, so the island looks at the screen itself: a live copy of
// the strip at the top of its monitor (Windows.Graphics.Capture), which the
// glass then blurs lightly and bends at its edges.
//
// - It runs only while the island shows (Start/Stop): never while tucked
//   away, hidden for a game, or idle.
// - The island's own window is left out of every capture (otherwise it would
//   see itself), which also keeps it out of screenshots and recordings.
// - New frames come only when the screen behind changes, at most ~60 a second.
class GlassCapture
{
public:
    GlassCapture() = default;
    GlassCapture(const GlassCapture&) = delete;
    GlassCapture& operator=(const GlassCapture&) = delete;
    ~GlassCapture() { Stop(); }

    // `snapshot` (tests only, --glass-snapshot): the island isn't left out
    // of captures, so test screenshots can see it; one frame is taken before
    // it first shows, and kept.
    void Initialize(HWND window, Graphics& graphics, bool snapshot);

    // Clear glass on or off (glass.style). On leaves the island out of every
    // capture; off puts it back into screenshots. Returns false when this
    // Windows can't capture (then the glass stays frosted).
    bool SetEnabled(bool enabled);
    bool Enabled() const { return m_enabled; }
    bool SnapshotPending() const { return m_snapshot && !m_snapshotTaken; }

    // The part of the monitor to copy, in its pixels: `width` wide and
    // centred, from the top down `height`.
    void SetRegion(HMONITOR monitor, int width, int height);

    // The island's own window, in the monitor's pixels. Changes only there
    // are the island itself redrawing (it's left out of the picture), so
    // they're skipped: otherwise the glass updating would itself count as a
    // change behind it, again and again (measured: 25 % of the GPU).
    void SetOwnWindow(const RECT& rect) { m_ownWindow = rect; }

    // At most this many new pictures a second. Each one costs the GPU a
    // little (Windows composes the screen once more, without the island),
    // so the small pill asks for few and the open panel for more.
    void SetMaxFps(int fps);

    void Start();
    void Stop();

    // The copy, region-sized. Called once a fresh frame has been copied after Start.
    winrt::Windows::UI::Composition::CompositionDrawingSurface Surface() const { return m_surface; }
    std::function<void()> onFrame;

private:
    void OnFrame();
    bool ChangedBehind(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame& frame, const RECT& strip) const;

    HWND m_window{};
    Graphics* m_graphics = nullptr;
    bool m_available = false;
    bool m_enabled = false;
    bool m_snapshot = false;
    bool m_snapshotTaken = false;
    bool m_running = false;
    bool m_freshFrame = false;  // a frame arrived since Start
    uint64_t m_started = 0;     // when Start ran (for the log)
    int m_starts = 0;           // the first few starts are logged
    HMONITOR m_monitor{};
    int m_width = 0, m_height = 0;
    RECT m_ownWindow{};
    int m_fps = 60;
    int m_skipped = 0;  // frames skipped since Start (for the log)
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_surface{ nullptr };
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::com_ptr<ID3D11Device> m_d3d;  // the device m_device wraps
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_poolSize{};
};
