#include "services/AudioService.h"

#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>

#include "platform/Log.h"

namespace
{
    // Windows calls this (on its own thread) whenever the volume or mute changes.
    struct VolumeEvents : winrt::implements<VolumeEvents, IAudioEndpointVolumeCallback>
    {
        std::function<void(float volume, bool muted, GUID context)> onChange;

        HRESULT __stdcall OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) noexcept override
        {
            if (data && onChange)
            {
                onChange(data->fMasterVolume, data->bMuted != FALSE, data->guidEventContext);
            }
            return S_OK;
        }
    };

    // ...and this when the default output device changes (headphones plugged in).
    struct DeviceEvents : winrt::implements<DeviceEvents, IMMNotificationClient>
    {
        std::function<void()> onDefaultChanged;

        HRESULT __stdcall OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) noexcept override
        {
            if (flow == eRender && role == eMultimedia && onDefaultChanged)
            {
                onDefaultChanged();
            }
            return S_OK;
        }
        HRESULT __stdcall OnDeviceStateChanged(LPCWSTR, DWORD) noexcept override { return S_OK; }
        HRESULT __stdcall OnDeviceAdded(LPCWSTR) noexcept override { return S_OK; }
        HRESULT __stdcall OnDeviceRemoved(LPCWSTR) noexcept override { return S_OK; }
        HRESULT __stdcall OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) noexcept override { return S_OK; }
    };

    std::wstring DeviceName(IMMDevice* device)
    {
        winrt::com_ptr<IPropertyStore> properties;
        if (FAILED(device->OpenPropertyStore(STGM_READ, properties.put())))
        {
            return {};
        }
        // "Speakers" / "Headphones" (DeviceDesc) is shorter than the full
        // "Speakers (Realtek(R) Audio)" friendly name.
        PROPVARIANT value;
        PropVariantInit(&value);
        std::wstring name;
        if (SUCCEEDED(properties->GetValue(PKEY_Device_DeviceDesc, &value)) && value.vt == VT_LPWSTR)
        {
            name = value.pwszVal;
        }
        PropVariantClear(&value);
        return name;
    }
}

AudioService::~AudioService()
{
    m_alive.reset();
    Unbind();
    if (m_devices && m_deviceEvents)
    {
        m_devices->UnregisterEndpointNotificationCallback(m_deviceEvents.get());
    }
}

void AudioService::Start(std::function<void(const AudioState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    CoCreateGuid(&m_ownChanges);

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(m_devices.put()))))
    {
        Log::Info(L"audio: unavailable");
        return;
    }

    // Both callbacks come from Windows' own threads: hop to the UI thread.
    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    auto volumeEvents = winrt::make_self<VolumeEvents>();
    volumeEvents->onChange = [queue, alive, this](float volume, bool muted, GUID context)
    {
        queue.TryEnqueue([alive, this, volume, muted, context]
        {
            // Our own changes come back as events too; the state already has them.
            if (alive.expired() || context == m_ownChanges)
            {
                return;
            }
            m_state.volume = volume;
            m_state.muted = muted;
            Publish();
        });
    };
    m_volumeEvents = volumeEvents.as<IAudioEndpointVolumeCallback>();

    auto deviceEvents = winrt::make_self<DeviceEvents>();
    deviceEvents->onDefaultChanged = [queue, alive, this]
    {
        queue.TryEnqueue([alive, this]
        {
            if (!alive.expired())
            {
                Bind();
            }
        });
    };
    m_deviceEvents = deviceEvents.as<IMMNotificationClient>();
    m_devices->RegisterEndpointNotificationCallback(m_deviceEvents.get());

    Bind();
}

void AudioService::Bind()
{
    Unbind();
    m_state = AudioState{};
    winrt::com_ptr<IMMDevice> device;
    if (SUCCEEDED(m_devices->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put())) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, m_endpoint.put_void())))
    {
        m_endpoint->RegisterControlChangeNotify(m_volumeEvents.get());
        BOOL muted = FALSE;
        m_endpoint->GetMasterVolumeLevelScalar(&m_state.volume);
        m_endpoint->GetMute(&muted);
        m_state.muted = muted != FALSE;
        m_state.device = DeviceName(device.get());
        m_state.available = true;
        Log::Info(L"audio: output is {}", m_state.device.empty() ? L"(unnamed)" : m_state.device);
    }
    else
    {
        m_endpoint = nullptr;
        Log::Info(L"audio: no output device");
    }
    Publish();
}

void AudioService::Unbind()
{
    if (m_endpoint)
    {
        m_endpoint->UnregisterControlChangeNotify(m_volumeEvents.get());
        m_endpoint = nullptr;
    }
}

void AudioService::SetVolume(float volume)
{
    if (!m_endpoint)
    {
        return;
    }
    volume = std::clamp(volume, 0.0f, 1.0f);
    m_state.volume = volume;
    m_endpoint->SetMasterVolumeLevelScalar(volume, &m_ownChanges);
    // Like Windows' own slider: turning the volume up unmutes.
    if (m_state.muted && volume > 0.0f)
    {
        m_state.muted = false;
        m_endpoint->SetMute(FALSE, &m_ownChanges);
    }
    Publish();
}

void AudioService::ToggleMute()
{
    if (m_endpoint)
    {
        m_state.muted = !m_state.muted;
        m_endpoint->SetMute(m_state.muted, &m_ownChanges);
        Publish();
    }
}

void AudioService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
