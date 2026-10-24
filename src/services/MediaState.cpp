#include "services/MediaState.h"

#include <algorithm>
#include <chrono>
#include <cwctype>

double MediaState::PositionAt(winrt::Windows::Foundation::DateTime now) const
{
    double position = positionSeconds;
    if (playing && positionMeasuredAt.time_since_epoch().count() != 0)
    {
        const double elapsed = std::chrono::duration<double>(now - positionMeasuredAt).count();
        position += std::max(0.0, elapsed) * rate;
    }
    return std::clamp(position, 0.0, std::max(0.0, durationSeconds));
}

int PickSession(const std::vector<SessionSummary>& sessions, int manualChoice, int shown)
{
    const int count = static_cast<int>(sessions.size());
    if (count == 0)
    {
        return -1;
    }
    if (manualChoice >= 0 && manualChoice < count)
    {
        return manualChoice;
    }
    auto mostRecent = [&](bool playingOnly)
    {
        int best = -1;
        for (int i = 0; i < static_cast<int>(sessions.size()); ++i)
        {
            if ((!playingOnly || sessions[i].playing) &&
                (best < 0 || sessions[i].lastActivity > sessions[best].lastActivity))
            {
                best = i;
            }
        }
        return best;
    };
    if (const int playing = mostRecent(true); playing >= 0)
    {
        return playing;
    }
    if (shown >= 0 && shown < count)
    {
        return shown;
    }
    for (int i = 0; i < count; ++i)
    {
        if (sessions[i].sameAppAsShown)
        {
            return i;
        }
    }
    for (int i = 0; i < static_cast<int>(sessions.size()); ++i)
    {
        if (sessions[i].windowsCurrent)
        {
            return i;
        }
    }
    return mostRecent(false);
}

std::wstring FriendlyAppName(std::wstring_view id)
{
    std::wstring lower(id);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    struct Known
    {
        const wchar_t* fragment;
        const wchar_t* name;
    };
    static constexpr Known known[] = {
        { L"spotify", L"Spotify" },
        { L"msedge", L"Edge" },
        { L"chrome", L"Chrome" },
        { L"firefox", L"Firefox" },
        { L"308046b0af4a39cb", L"Firefox" },  // Firefox's app id
        { L"zunemusic", L"Media Player" },
        { L"zunevideo", L"Movies & TV" },
        { L"vlc", L"VLC" },
        { L"brave", L"Brave" },
        { L"opera", L"Opera" },
        { L"applemusic", L"Apple Music" },
        { L"discord", L"Discord" },
    };
    for (const Known& entry : known)
    {
        if (lower.find(entry.fragment) != std::wstring::npos)
        {
            return entry.name;
        }
    }

    // Otherwise make something readable from the id: drop a Store app's
    // "!App" suffix and "_publisherhash", any folders and ".exe".
    std::wstring name(id);
    if (const size_t bang = name.find(L'!'); bang != std::wstring::npos)
    {
        name.resize(bang);
    }
    if (const size_t slash = name.find_last_of(L"\\/"); slash != std::wstring::npos)
    {
        name.erase(0, slash + 1);
    }
    if (const size_t underscore = name.find(L'_'); underscore != std::wstring::npos)
    {
        name.resize(underscore);
    }
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0)
    {
        name.resize(name.size() - 4);
    }
    if (const size_t dot = name.find_last_of(L'.'); dot != std::wstring::npos && dot + 1 < name.size())
    {
        name.erase(0, dot + 1);  // "Publisher.AppName" -> "AppName"
    }
    if (!name.empty())
    {
        name[0] = static_cast<wchar_t>(std::towupper(name[0]));
    }
    return name.empty() ? L"Media" : name;
}

winrt::com_ptr<IWICBitmapSource> DecodeArtwork(IStream* image, UINT pixels)
{
    winrt::com_ptr<IWICImagingFactory> wic;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())));

    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(wic->CreateDecoderFromStream(image, nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()));
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    UINT width = 0, height = 0;
    winrt::check_hresult(frame->GetSize(&width, &height));
    if (width == 0 || height == 0)
    {
        return nullptr;
    }

    const double scale = static_cast<double>(pixels) / std::min(width, height);
    const UINT scaledWidth = std::max(pixels, static_cast<UINT>(width * scale + 0.5));
    const UINT scaledHeight = std::max(pixels, static_cast<UINT>(height * scale + 0.5));
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::check_hresult(wic->CreateBitmapScaler(scaler.put()));
    winrt::check_hresult(scaler->Initialize(frame.get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeHighQualityCubic));

    const WICRect square{ static_cast<INT>((scaledWidth - pixels) / 2), static_cast<INT>((scaledHeight - pixels) / 2),
                          static_cast<INT>(pixels), static_cast<INT>(pixels) };
    winrt::com_ptr<IWICBitmapClipper> clipper;
    winrt::check_hresult(wic->CreateBitmapClipper(clipper.put()));
    winrt::check_hresult(clipper->Initialize(scaler.get(), &square));

    // The pixel format Direct2D draws directly (premultiplied BGRA).
    winrt::com_ptr<IWICFormatConverter> converter;
    winrt::check_hresult(wic->CreateFormatConverter(converter.put()));
    winrt::check_hresult(converter->Initialize(clipper.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeMedianCut));

    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(wic->CreateBitmapFromSource(converter.get(), WICBitmapCacheOnLoad, bitmap.put()));
    return bitmap.as<IWICBitmapSource>();
}
