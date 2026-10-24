#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <functional>
#include <memory>
#include <string>

#include <winrt/base.h>
#include <winrt/Windows.System.h>

struct AudioState
{
    bool available = false;  // there is an output device
    float volume = 0.0f;     // 0..1
    bool muted = false;
    std::wstring device;     // "Speakers", "Headphones", ...
};

// The system volume of the default output device (Windows' Core Audio).
// Changes made anywhere (volume keys, Quick Settings) arrive as events, and
// it follows the default device when you plug in headphones (PLAN.md E6).
// Results are delivered on the UI thread.
class AudioService
{
public:
    AudioService() = default;
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
    ~AudioService();

    void Start(std::function<void(const AudioState&)> onChange);
    void SetVolume(float volume);
    void ToggleMute();
    const AudioState& Current() const { return m_state; }

private:
    void Bind();
    void Unbind();
    void Publish();

    std::function<void(const AudioState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    winrt::com_ptr<IMMDeviceEnumerator> m_devices;
    winrt::com_ptr<IAudioEndpointVolume> m_endpoint;
    winrt::com_ptr<IAudioEndpointVolumeCallback> m_volumeEvents;
    winrt::com_ptr<IMMNotificationClient> m_deviceEvents;
    GUID m_ownChanges{};  // tags our own changes, so their echoes can be ignored (D9)
    AudioState m_state;
};
