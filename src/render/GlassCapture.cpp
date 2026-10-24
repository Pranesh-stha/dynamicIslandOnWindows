#include "render/GlassCapture.h"

#include <d2d1_1.h>
#include <dxgi.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <chrono>

#include "platform/Log.h"
#include "render/Graphics.h"

namespace capture = winrt::Windows::Graphics::Capture;
namespace d3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

void GlassCapture::Initialize(HWND window, Graphics& graphics, bool snapshot)
{
    m_window = window;
    m_graphics = &graphics;
    m_snapshot = snapshot;
    try
    {
        m_available = capture::GraphicsCaptureSession::IsSupported();
        if (m_available)
        {
            m_surface = graphics.CreateSurface();
        }
    }
    catch (const winrt::hresult_error&)
    {
        m_available = false;
    }
    if (!m_available)
    {
        Log::Info(L"glass: screen capture not supported here; frosted glass");
    }
}

bool GlassCapture::SetEnabled(bool enabled)
{
    enabled = enabled && m_available;
    if (enabled == m_enabled)
    {
        return m_enabled;
    }
    if (!enabled)
    {
        Stop();
    }
    // The island must never see itself in what it captures. (Tests'
    // snapshot mode skips this, so their screenshots still see the island.)
    const DWORD affinity = enabled && !m_snapshot ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    if (!SetWindowDisplayAffinity(m_window, affinity) && enabled && !m_snapshot)
    {
        Log::Info(L"glass: can't leave the island out of captures ({}); frosted glass", GetLastError());
        return false;
    }
    m_enabled = enabled;
    Log::Info(L"glass: {}", enabled ? L"clear (the island is left out of screenshots and recordings)" : L"frosted");
    return m_enabled;
}

void GlassCapture::SetRegion(HMONITOR monitor, int width, int height)
{
    const bool monitorChanged = monitor != m_monitor;
    m_monitor = monitor;
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    if (monitorChanged && m_running)
    {
        // The island moved to another monitor: capture that one instead.
        Stop();
        Start();
    }
}

void GlassCapture::Start()
{
    if (!m_enabled || m_running || !m_monitor)
    {
        return;
    }
    if (m_snapshot && m_snapshotTaken)
    {
        if (onFrame)
        {
            onFrame();  // tests: the one frame taken before the island showed
        }
        return;
    }
    try
    {
        // A Direct3D device the capture can hand frames to: the one we draw with.
        ID3D11Device* current = m_graphics->D3DDevice();
        if (!m_device || m_d3d.get() != current)
        {
            m_d3d.copy_from(current);
            winrt::com_ptr<::IInspectable> inspectable;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(m_d3d.as<IDXGIDevice>().get(), inspectable.put()));
            m_device = inspectable.as<d3d::IDirect3DDevice>();
            m_pool = nullptr;
        }
        if (!m_item)
        {
            auto interop = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            winrt::check_hresult(interop->CreateForMonitor(m_monitor, winrt::guid_of<capture::GraphicsCaptureItem>(),
                winrt::put_abi(m_item)));
        }
        m_poolSize = m_item.Size();
        // Frames arrive on this (the UI) thread, through its dispatcher queue.
        m_pool = capture::Direct3D11CaptureFramePool::Create(m_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_poolSize);
        m_pool.FrameArrived([this](auto&&, auto&&) { OnFrame(); });
        m_session = m_pool.CreateCaptureSession(m_item);
        m_session.IsCursorCaptureEnabled(false);
        try
        {
            m_session.IsBorderRequired(false);  // no yellow frame around the screen
        }
        catch (const winrt::hresult_error& error)
        {
            Log::Info(L"glass: Windows insists on its capture border ({}); frosted glass", error.message());
            Stop();
            m_available = false;
            m_enabled = false;
            SetWindowDisplayAffinity(m_window, WDA_NONE);
            return;
        }
        try
        {
            m_session.MinUpdateInterval(std::chrono::milliseconds(16));  // at most ~60 frames a second
        }
        catch (const winrt::hresult_error&)
        {
        }
        m_freshFrame = false;
        m_started = GetTickCount64();
        m_session.StartCapture();
        m_running = true;
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"glass: capture didn't start ({}); frosted glass", error.message());
        m_available = false;
        Stop();
    }
}

void GlassCapture::Stop()
{
    m_running = false;
    if (m_session)
    {
        m_session.Close();
        m_session = nullptr;
    }
    if (m_pool)
    {
        m_pool.Close();
        m_pool = nullptr;
    }
}

void GlassCapture::OnFrame()
{
    if (!m_pool)
    {
        return;
    }
    const auto frame = m_pool.TryGetNextFrame();
    if (!frame)
    {
        return;
    }
    const auto size = frame.ContentSize();
    if (size.Width != m_poolSize.Width || size.Height != m_poolSize.Height)
    {
        // The monitor's resolution changed: take the next frame at the new size.
        m_poolSize = size;
        m_pool.Recreate(m_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        return;
    }

    // Copy the strip behind the island into the glass's surface (on the GPU).
    winrt::com_ptr<IDXGISurface> source;
    winrt::check_hresult(frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()->GetInterface(
        IID_PPV_ARGS(source.put())));
    const float left = std::floor((static_cast<float>(size.Width) - static_cast<float>(m_width)) / 2.0f);
    const D2D1_RECT_F from{ left, 0.0f, left + static_cast<float>(m_width), static_cast<float>(m_height) };
    m_graphics->Draw(m_surface, static_cast<float>(m_width), static_cast<float>(m_height), [&](ID2D1DeviceContext* context)
    {
        winrt::com_ptr<ID2D1Bitmap1> bitmap;
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (SUCCEEDED(context->CreateBitmapFromDxgiSurface(source.get(), &properties, bitmap.put())))
        {
            const D2D1_RECT_F to{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
            context->DrawBitmap(bitmap.get(), &to, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &from);
        }
    });

    const bool first = !m_freshFrame;
    m_freshFrame = true;
    if (first && m_starts++ < 3)
    {
        Log::Info(L"glass: first frame {} ms after the island appeared", GetTickCount64() - m_started);
    }
    if (m_snapshot)
    {
        // Tests: keep this one frame. Stopped from the queue, not inside the pool's own event.
        m_snapshotTaken = true;
        winrt::Windows::System::DispatcherQueue::GetForCurrentThread().TryEnqueue([this] { Stop(); });
    }
    if (first && onFrame)
    {
        onFrame();  // the glass can show it now
    }
}
