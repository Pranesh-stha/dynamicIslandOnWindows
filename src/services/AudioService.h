#pragma once

#include <windows.h>
#include <Unknwn.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.System.h>

// One app in the volume mixer.
struct AppVolume
{
    std::wstring key;      // groups an app's sounds: its exe name, or "system"
    std::wstring name;     // "Chrome", "Spotify", "System sounds"
    float volume = 1.0f;   // 0..1, relative to the main volume
    bool muted = false;
    bool active = false;   // playing sound right now
    bool system = false;   // Windows' own sounds
};

struct AudioState
{
    bool available = false;  // there is an output device
    float volume = 0.0f;     // 0..1
    bool muted = false;
    std::wstring device;     // "Speakers", "Headphones", ...
    std::vector<AppVolume> apps;  // playing apps first, Windows' sounds last

    bool micAvailable = false;  // there is a microphone
    bool micMuted = false;      // the default microphone is muted
    std::wstring mic;           // its name
};

// One audio session (an app's stream) as the mixer sees it.
struct SessionInfo
{
    std::wstring key;
    std::wstring name;
    float volume = 1.0f;
    bool muted = false;
    bool active = false;
    bool system = false;
};

// Merges an app's sessions into one mixer row (Chrome alone can have
// several) and orders them: playing apps first, then by name, Windows' own
// sounds last. Pure logic, unit-tested.
std::vector<AppVolume> GroupSessions(const std::vector<SessionInfo>& sessions);

// "chrome.exe" -> "Chrome", "VALORANT-Win64-Shipping.exe" -> "VALORANT".
std::wstring FriendlyExeName(std::wstring_view exe);

// The system volume of the default output device, the per-app mixer and
// the microphone's mute (Windows' Core Audio). Changes made anywhere (volume
// keys, Quick Settings, Windows' mixer) arrive as events, and it follows the
// default devices when you plug in headphones (PLAN.md E6). Results are
// delivered on the UI thread.
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
    void SetAppVolume(const std::wstring& key, float volume);
    void ToggleAppMute(const std::wstring& key);
    // Mutes or unmutes every microphone, so no app can still listen through another one.
    void ToggleMicMute();
    const AudioState& Current() const { return m_state; }

private:
    struct Session
    {
        winrt::com_ptr<IAudioSessionControl2> control;
        winrt::com_ptr<ISimpleAudioVolume> volume;
        winrt::com_ptr<IAudioSessionEvents> events;
        SessionInfo info;
    };

    void Bind();
    void Unbind();
    void BindMic();
    void AddSession(IAudioSessionControl* control);
    void OnSessionChanged(IAudioSessionControl2* control);
    void UpdateApps();
    void Publish();

    std::function<void(const AudioState&)> m_onChange;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    std::shared_ptr<bool> m_alive;
    winrt::com_ptr<IMMDeviceEnumerator> m_devices;
    winrt::com_ptr<IAudioEndpointVolume> m_endpoint;
    winrt::com_ptr<IAudioEndpointVolumeCallback> m_volumeEvents;
    winrt::com_ptr<IAudioEndpointVolume> m_mic;
    winrt::com_ptr<IAudioEndpointVolumeCallback> m_micEvents;
    winrt::com_ptr<IMMNotificationClient> m_deviceEvents;
    winrt::com_ptr<IAudioSessionManager2> m_sessionManager;
    winrt::com_ptr<IAudioSessionNotification> m_sessionCreated;
    std::vector<Session> m_sessions;
    GUID m_ownChanges{};  // tags our own changes, so their echoes can be ignored (D9)
    AudioState m_state;
};
