#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <wincodec.h>

#include <functional>
#include <string>
#include <string_view>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>

struct TextStyle
{
    float sizePx = 14.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool icon = false;        // use the icon font (Segoe Fluent Icons) instead of Segoe UI
    float maxWidthPx = 0.0f;  // 0 = no limit; longer text ends in "…"
    int maxLines = 1;         // with maxWidthPx: wrap onto up to this many lines, the last one ending in "…"
};

// The GPU side of drawing. Text, icons and images are drawn once into
// "surfaces" with Direct2D, and the compositor then shows and animates them
// without anything being redrawn.
//
// It uses the power-saving GPU (the Intel one), so drawing a pill never wakes
// the NVIDIA GPU.
class Graphics
{
public:
    using Size = winrt::Windows::Foundation::Size;

    void Initialize(const winrt::Windows::UI::Composition::Compositor& compositor);

    // Called (on the UI thread) after a GPU reset, e.g. a driver update. All
    // surfaces are then blank and must be drawn again.
    void OnDeviceReplaced(std::function<void()> handler) { m_onDeviceReplaced = std::move(handler); }

    winrt::Windows::UI::Composition::CompositionDrawingSurface CreateSurface();

    // Draws into `surface`, resizing it to width x height pixels first.
    // Drawing coordinates are pixels.
    void Draw(const winrt::Windows::UI::Composition::CompositionDrawingSurface& surface, float width, float height,
              const std::function<void(ID2D1DeviceContext*)>& draw);

    // Draws white text, sized to fit, and returns the surface size in pixels.
    Size RenderText(const winrt::Windows::UI::Composition::CompositionDrawingSurface& surface,
                  std::wstring_view text, const TextStyle& style);
    Size MeasureText(std::wstring_view text, const TextStyle& style);

    IWICImagingFactory* Wic() const { return m_wic.get(); }
    const std::wstring& TextFont() const { return m_textFont; }

private:
    winrt::com_ptr<ID2D1Device> CreateRenderingDevice();
    winrt::com_ptr<IDWriteTextLayout> CreateLayout(std::wstring_view text, const TextStyle& style);
    bool HasFont(const wchar_t* family);
    void RecoverFromDeviceLoss();

    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::CompositionGraphicsDevice m_device{ nullptr };
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    winrt::com_ptr<ID2D1Factory1> m_d2dFactory;
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<IDWriteFactory> m_dwrite;
    winrt::com_ptr<IWICImagingFactory> m_wic;
    std::wstring m_textFont;
    std::wstring m_iconFont;
    std::function<void()> m_onDeviceReplaced;
};
