#include "render/Graphics.h"

#include <dxgi1_6.h>
#include <windows.ui.composition.interop.h>

#include <winrt/Windows.Graphics.DirectX.h>

#include <algorithm>
#include <cmath>

#include "platform/Log.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace abi = ABI::Windows::UI::Composition;

void Graphics::Initialize(const wuc::Compositor& compositor)
{
    m_compositor = compositor;
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

    winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), nullptr, m_d2dFactory.put_void()));
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwrite.put())));
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(m_wic.put())));

    // Windows 11 fonts, with the Windows 10 ones as a fallback.
    m_textFont = HasFont(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    m_iconFont = HasFont(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

    // The compositor draws our surfaces with this Direct2D device.
    const auto renderingDevice = CreateRenderingDevice();
    auto interop = compositor.as<abi::ICompositorInterop>();
    winrt::check_hresult(interop->CreateGraphicsDevice(renderingDevice.get(),
        reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(m_device))));
}

winrt::com_ptr<ID2D1Device> Graphics::CreateRenderingDevice()
{
    // Ask Windows for the GPU that uses the least power.
    winrt::com_ptr<IDXGIAdapter1> adapter;
    winrt::com_ptr<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put()))))
    {
        factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(adapter.put()));
    }

    // BGRA support is what lets Direct2D draw on a Direct3D device.
    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    m_d3dDevice = nullptr;
    HRESULT result = D3D11CreateDevice(adapter.get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_d3dDevice.put(), nullptr, nullptr);
    if (FAILED(result))
    {
        // No usable GPU (e.g. Remote Desktop): draw on the CPU instead.
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
            m_d3dDevice.put(), nullptr, nullptr);
    }
    winrt::check_hresult(result);

    if (adapter)
    {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        Log::Info(L"drawing on GPU: {}", description.Description);
    }

    winrt::com_ptr<ID2D1Device> d2dDevice;
    winrt::check_hresult(m_d2dFactory->CreateDevice(m_d3dDevice.as<IDXGIDevice>().get(), d2dDevice.put()));
    return d2dDevice;
}

bool Graphics::HasFont(const wchar_t* family)
{
    winrt::com_ptr<IDWriteFontCollection> fonts;
    if (FAILED(m_dwrite->GetSystemFontCollection(fonts.put())))
    {
        return false;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

wuc::CompositionDrawingSurface Graphics::CreateSurface()
{
    using namespace winrt::Windows::Graphics::DirectX;
    return m_device.CreateDrawingSurface({ 1.0f, 1.0f }, DirectXPixelFormat::B8G8R8A8UIntNormalized, DirectXAlphaMode::Premultiplied);
}

void Graphics::Draw(const wuc::CompositionDrawingSurface& surface, float width, float height,
                    const std::function<void(ID2D1DeviceContext*)>& draw)
{
    auto interop = surface.as<abi::ICompositionDrawingSurfaceInterop>();
    const SIZE size{ std::max(1L, static_cast<LONG>(std::ceil(width))), std::max(1L, static_cast<LONG>(std::ceil(height))) };
    const auto current = surface.SizeInt32();
    if (current.Width != size.cx || current.Height != size.cy)
    {
        winrt::check_hresult(interop->Resize(size));
    }

    winrt::com_ptr<ID2D1DeviceContext> context;
    POINT offset{};
    const HRESULT result = interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset);
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
    {
        RecoverFromDeviceLoss();
        return;
    }
    winrt::check_hresult(result);

    // The surface may live inside a larger shared texture: `offset` says where.
    context->SetDpi(96.0f, 96.0f);  // 1 drawing unit = 1 pixel
    context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    draw(context.get());
    interop->EndDraw();
}

void Graphics::RecoverFromDeviceLoss()
{
    Log::Info(L"GPU was reset; recreating the drawing device");
    const auto renderingDevice = CreateRenderingDevice();
    m_device.as<abi::ICompositionGraphicsDeviceInterop>()->SetRenderingDevice(renderingDevice.get());
    // Redraw later, not inside whatever draw call noticed the loss.
    m_queue.TryEnqueue([this]
    {
        if (m_onDeviceReplaced)
        {
            m_onDeviceReplaced();
        }
    });
}

winrt::com_ptr<IDWriteTextLayout> Graphics::CreateLayout(std::wstring_view text, const TextStyle& style)
{
    winrt::com_ptr<IDWriteTextFormat> format;
    winrt::check_hresult(m_dwrite->CreateTextFormat(style.icon ? m_iconFont.c_str() : m_textFont.c_str(), nullptr,
        style.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.sizePx, L"", format.put()));
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const bool limited = style.maxWidthPx > 0.0f;
    if (limited)
    {
        DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        winrt::com_ptr<IDWriteInlineObject> ellipsis;
        m_dwrite->CreateEllipsisTrimmingSign(format.get(), ellipsis.put());
        format->SetTrimming(&trimming, ellipsis.get());
    }

    winrt::com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(m_dwrite->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.get(),
        limited ? style.maxWidthPx : 100000.0f, 10000.0f, layout.put()));
    return layout;
}

Graphics::Size Graphics::MeasureText(std::wstring_view text, const TextStyle& style)
{
    DWRITE_TEXT_METRICS metrics{};
    CreateLayout(text, style)->GetMetrics(&metrics);
    float width = metrics.widthIncludingTrailingWhitespace;
    if (style.maxWidthPx > 0.0f)
    {
        width = std::min(width, style.maxWidthPx);
    }
    // One pixel of room on each side for anti-aliased edges.
    return { std::ceil(width) + 2.0f, std::ceil(metrics.height) };
}

Graphics::Size Graphics::RenderText(const wuc::CompositionDrawingSurface& surface, std::wstring_view text, const TextStyle& style)
{
    const auto layout = CreateLayout(text, style);
    const Size size = MeasureText(text, style);
    Draw(surface, size.Width, size.Height, [&](ID2D1DeviceContext* context)
    {
        winrt::com_ptr<ID2D1SolidColorBrush> white;
        context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), white.put());
        // Grayscale smoothing: ClearType needs a solid background, and ours is glass.
        context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        context->DrawTextLayout({ 1.0f, 0.0f }, layout.get(), white.get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    });
    return size;
}
