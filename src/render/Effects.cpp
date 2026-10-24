#include "render/Effects.h"

#include <d2d1effects.h>
#include <windows.graphics.effects.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>

#include <array>
#include <vector>

#include "platform/Log.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace effects = winrt::Windows::Graphics::Effects;
using winrt::Windows::Foundation::IPropertyValue;
using winrt::Windows::Foundation::PropertyValue;

namespace
{
    // Describes one Direct2D effect to the compositor: which effect it is,
    // its settings (by index, as Direct2D numbers them) and its inputs. C#
    // apps get classes like this from Win2D; in C++ we write the small
    // interface the compositor reads ourselves.
    struct D2DEffect : winrt::implements<D2DEffect, effects::IGraphicsEffect, effects::IGraphicsEffectSource,
                                         ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop>
    {
        D2DEffect(const GUID& id, std::vector<IPropertyValue> properties, std::vector<effects::IGraphicsEffectSource> sources)
            : m_id(id), m_properties(std::move(properties)), m_sources(std::move(sources)) {}

        winrt::hstring Name() { return m_name; }
        void Name(const winrt::hstring& name) { m_name = name; }

        HRESULT __stdcall GetEffectId(GUID* id) noexcept override
        {
            *id = m_id;
            return S_OK;
        }

        HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT*, ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override
        {
            return E_INVALIDARG;  // we never animate effect properties by name
        }

        HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override
        {
            *count = static_cast<UINT>(m_properties.size());
            return S_OK;
        }

        HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept override
        {
            if (index >= m_properties.size())
            {
                return E_INVALIDARG;
            }
            winrt::copy_to_abi(m_properties[index], *reinterpret_cast<void**>(value));
            return S_OK;
        }

        HRESULT __stdcall GetSource(UINT index, ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) noexcept override
        {
            if (index >= m_sources.size())
            {
                return E_INVALIDARG;
            }
            winrt::copy_to_abi(m_sources[index], *reinterpret_cast<void**>(source));
            return S_OK;
        }

        HRESULT __stdcall GetSourceCount(UINT* count) noexcept override
        {
            *count = static_cast<UINT>(m_sources.size());
            return S_OK;
        }

    private:
        GUID m_id;
        winrt::hstring m_name;
        std::vector<IPropertyValue> m_properties;
        std::vector<effects::IGraphicsEffectSource> m_sources;
    };

    IPropertyValue Value(uint32_t value)
    {
        return PropertyValue::CreateUInt32(value).as<IPropertyValue>();
    }

    IPropertyValue Value(const std::array<float, 20>& values)
    {
        return PropertyValue::CreateSingleArray(values).as<IPropertyValue>();
    }
}

wuc::CompositionBrush Effects::CreateBackdropBrush(const wuc::Compositor& compositor, float saturation, float brightness)
{
    // A colour matrix: each output colour is a mix of the input colours.
    // Mixing towards the grey value (weighted by how bright the eye sees red,
    // green and blue) desaturates; mixing away from it boosts colour.
    const float s = saturation;
    const float b = brightness;
    constexpr float r = 0.2126f, g = 0.7152f, bl = 0.0722f;
    const std::array<float, 20> matrix{
        (r * (1 - s) + s) * b, r * (1 - s) * b,       r * (1 - s) * b,        0,  // input red ->   R G B A
        g * (1 - s) * b,       (g * (1 - s) + s) * b, g * (1 - s) * b,        0,  // input green -> R G B A
        bl * (1 - s) * b,      bl * (1 - s) * b,      (bl * (1 - s) + s) * b, 0,  // input blue ->  R G B A
        0,                     0,                     0,                      1,  // alpha unchanged
        0,                     0,                     0,                      0,  // no offset
    };

    const auto backdrop = compositor.CreateHostBackdropBrush();

    try
    {
        // The compositor wants every property of the effect, in Direct2D's
        // own numbering: the matrix, the alpha mode and "clamp the output".
        const auto effect = winrt::make<D2DEffect>(CLSID_D2D1ColorMatrix,
            std::vector<IPropertyValue>{ Value(matrix), Value(static_cast<uint32_t>(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED)),
                                         PropertyValue::CreateBoolean(true).as<IPropertyValue>() },
            std::vector<effects::IGraphicsEffectSource>{ wuc::CompositionEffectSourceParameter(L"Backdrop") });
        auto brush = compositor.CreateEffectFactory(effect).CreateBrush();
        brush.SetSourceParameter(L"Backdrop", backdrop);
        return brush;
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"glass colour filter unavailable ({}); using the plain blur", error.message());
        return backdrop;
    }
}

wuc::CompositionBrush Effects::CreateTiledBrush(const wuc::Compositor& compositor, const wuc::ICompositionSurface& tile)
{
    auto tileBrush = compositor.CreateSurfaceBrush(tile);
    tileBrush.Stretch(wuc::CompositionStretch::None);
    tileBrush.HorizontalAlignmentRatio(0.0f);
    tileBrush.VerticalAlignmentRatio(0.0f);
    try
    {
        // The border effect in "wrap" mode repeats its input image outwards forever.
        const auto effect = winrt::make<D2DEffect>(CLSID_D2D1Border,
            std::vector<IPropertyValue>{ Value(D2D1_BORDER_EDGE_MODE_WRAP), Value(D2D1_BORDER_EDGE_MODE_WRAP) },
            std::vector<effects::IGraphicsEffectSource>{ wuc::CompositionEffectSourceParameter(L"Tile") });
        auto brush = compositor.CreateEffectFactory(effect).CreateBrush();
        brush.SetSourceParameter(L"Tile", tileBrush);
        return brush;
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"tiling filter unavailable ({})", error.message());
        return tileBrush;
    }
}
