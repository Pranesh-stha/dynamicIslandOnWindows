#include "services/AudioService.h"

#include <functiondiscoverykeys_devpkey.h>
#include <Shlwapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "platform/Log.h"
#include "services/MediaState.h"

namespace
{
    // Windows calls this (on its own thread) whenever the main volume or mute changes.
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

    // ...and this when the default output or microphone changes (headphones plugged in).
    struct DeviceEvents : winrt::implements<DeviceEvents, IMMNotificationClient>
    {
        std::function<void()> onDefaultChanged;
        std::function<void()> onDefaultMicChanged;

        HRESULT __stdcall OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) noexcept override
        {
            if (flow == eCapture && role == eConsole && onDefaultMicChanged)
            {
                onDefaultMicChanged();
            }
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

    // An app started making sound (a new audio session).
    struct SessionCreated : winrt::implements<SessionCreated, IAudioSessionNotification>
    {
        std::function<void(IAudioSessionControl*)> onCreated;

        HRESULT __stdcall OnSessionCreated(IAudioSessionControl* session) noexcept override
        {
            if (session && onCreated)
            {
                onCreated(session);
            }
            return S_OK;
        }
    };

    // Something about one app's sound changed: its volume, playing or not, or it ended.
    struct SessionEvents : winrt::implements<SessionEvents, IAudioSessionEvents>
    {
        std::function<void(LPCGUID context)> onChange;

        HRESULT __stdcall OnSimpleVolumeChanged(float, BOOL, LPCGUID context) noexcept override { return Changed(context); }
        HRESULT __stdcall OnStateChanged(AudioSessionState) noexcept override { return Changed(nullptr); }
        HRESULT __stdcall OnSessionDisconnected(AudioSessionDisconnectReason) noexcept override { return Changed(nullptr); }
        HRESULT __stdcall OnDisplayNameChanged(LPCWSTR, LPCGUID) noexcept override { return S_OK; }
        HRESULT __stdcall OnIconPathChanged(LPCWSTR, LPCGUID) noexcept override { return S_OK; }
        HRESULT __stdcall OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) noexcept override { return S_OK; }
        HRESULT __stdcall OnGroupingParamChanged(LPCGUID, LPCGUID) noexcept override { return S_OK; }

    private:
        HRESULT Changed(LPCGUID context)
        {
            if (onChange)
            {
                onChange(context);
            }
            return S_OK;
        }
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

    // An app's exe name from the process list. Like the fullscreen watcher,
    // this never opens a handle to the process (games may be in the mixer).
    std::wstring ExeName(DWORD processId)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return {};
        }
        std::wstring name;
        PROCESSENTRY32W entry{ sizeof(entry) };
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        {
            if (entry.th32ProcessID == processId)
            {
                name = entry.szExeFile;
                break;
            }
        }
        CloseHandle(snapshot);
        return name;
    }

    std::wstring Lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), ::towlower);
        return text;
    }
}

std::wstring FriendlyExeName(std::wstring_view exe)
{
    std::wstring name(exe);
    // Unreal Engine games: "VALORANT-Win64-Shipping.exe".
    for (const wchar_t* suffix : { L"-Win64-Shipping", L"-Win32-Shipping", L"-WinGDK-Shipping" })
    {
        if (const size_t at = Lower(name).find(Lower(suffix)); at != std::wstring::npos)
        {
            name.erase(at, wcslen(suffix));
        }
    }
    return FriendlyAppName(name);
}

std::vector<AppVolume> GroupSessions(const std::vector<SessionInfo>& sessions)
{
    std::vector<AppVolume> apps;
    std::map<std::wstring, size_t> byKey;
    for (const SessionInfo& session : sessions)
    {
        if (session.key.empty())
        {
            continue;
        }
        const auto found = byKey.find(session.key);
        if (found == byKey.end())
        {
            byKey[session.key] = apps.size();
            apps.push_back({ session.key, session.name, session.volume, session.muted, session.active, session.system });
        }
        else
        {
            AppVolume& app = apps[found->second];
            app.active = app.active || session.active;
            app.muted = app.muted && session.muted;  // muted only if every part is
            app.volume = std::max(app.volume, session.volume);
        }
    }
    std::stable_sort(apps.begin(), apps.end(), [](const AppVolume& a, const AppVolume& b)
    {
        if (a.system != b.system) return !a.system;
        if (a.active != b.active) return a.active;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return apps;
}

AudioService::~AudioService()
{
    m_alive.reset();
    Unbind();
    if (m_mic)
    {
        m_mic->UnregisterControlChangeNotify(m_micEvents.get());
    }
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

    // All callbacks come from Windows' own threads: hop to the UI thread.
    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    const GUID own = m_ownChanges;
    auto volumeEvents = winrt::make_self<VolumeEvents>();
    volumeEvents->onChange = [queue, alive, this, own](float volume, bool muted, GUID context)
    {
        // Our own changes come back as events too; the state already has them.
        if (context == own)
        {
            return;
        }
        queue.TryEnqueue([alive, this, volume, muted]
        {
            if (!alive.expired())
            {
                m_state.volume = volume;
                m_state.muted = muted;
                Publish();
            }
        });
    };
    m_volumeEvents = volumeEvents.as<IAudioEndpointVolumeCallback>();

    auto micEvents = winrt::make_self<VolumeEvents>();
    micEvents->onChange = [queue, alive, this, own](float, bool muted, GUID context)
    {
        if (context == own)
        {
            return;
        }
        queue.TryEnqueue([alive, this, muted]
        {
            if (!alive.expired())
            {
                m_state.micMuted = muted;
                Publish();
            }
        });
    };
    m_micEvents = micEvents.as<IAudioEndpointVolumeCallback>();

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
    deviceEvents->onDefaultMicChanged = [queue, alive, this]
    {
        queue.TryEnqueue([alive, this]
        {
            if (!alive.expired())
            {
                BindMic();
                Publish();
            }
        });
    };
    m_deviceEvents = deviceEvents.as<IMMNotificationClient>();
    m_devices->RegisterEndpointNotificationCallback(m_deviceEvents.get());

    auto sessionCreated = winrt::make_self<SessionCreated>();
    sessionCreated->onCreated = [queue, alive, this](IAudioSessionControl* raw)
    {
        winrt::com_ptr<IAudioSessionControl> session;
        session.copy_from(raw);
        queue.TryEnqueue([alive, this, session]
        {
            if (!alive.expired())
            {
                AddSession(session.get());
                UpdateApps();
                Publish();
            }
        });
    };
    m_sessionCreated = sessionCreated.as<IAudioSessionNotification>();

    BindMic();
    Bind();
}

void AudioService::BindMic()
{
    if (m_mic)
    {
        m_mic->UnregisterControlChangeNotify(m_micEvents.get());
        m_mic = nullptr;
    }
    m_state.micAvailable = false;
    m_state.micMuted = false;
    m_state.mic.clear();
    winrt::com_ptr<IMMDevice> device;
    if (SUCCEEDED(m_devices->GetDefaultAudioEndpoint(eCapture, eConsole, device.put())) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, m_mic.put_void())))
    {
        m_mic->RegisterControlChangeNotify(m_micEvents.get());
        BOOL muted = FALSE;
        m_mic->GetMute(&muted);
        m_state.micAvailable = true;
        m_state.micMuted = muted != FALSE;
        m_state.mic = DeviceName(device.get());
        Log::Info(L"audio: microphone is {}{}", m_state.mic.empty() ? L"(unnamed)" : m_state.mic, m_state.micMuted ? L" (muted)" : L"");
    }
    else
    {
        m_mic = nullptr;
        Log::Info(L"audio: no microphone");
    }
}

void AudioService::Bind()
{
    Unbind();
    // A new output device: start over, except for the microphone.
    AudioState fresh;
    fresh.micAvailable = m_state.micAvailable;
    fresh.micMuted = m_state.micMuted;
    fresh.mic = m_state.mic;
    m_state = std::move(fresh);
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

        // The mixer: every app's sound on this device.
        if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, m_sessionManager.put_void())))
        {
            // Listing the sessions first is what makes Windows start sending "new session" events.
            winrt::com_ptr<IAudioSessionEnumerator> sessions;
            if (SUCCEEDED(m_sessionManager->GetSessionEnumerator(sessions.put())))
            {
                m_sessionManager->RegisterSessionNotification(m_sessionCreated.get());
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count; ++i)
                {
                    winrt::com_ptr<IAudioSessionControl> session;
                    if (SUCCEEDED(sessions->GetSession(i, session.put())))
                    {
                        AddSession(session.get());
                    }
                }
            }
        }
        UpdateApps();
        Log::Info(L"audio: apps in the mixer: {}", m_state.apps.size());
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
    for (Session& session : m_sessions)
    {
        session.control->UnregisterAudioSessionNotification(session.events.get());
    }
    m_sessions.clear();
    if (m_sessionManager)
    {
        m_sessionManager->UnregisterSessionNotification(m_sessionCreated.get());
        m_sessionManager = nullptr;
    }
    if (m_endpoint)
    {
        m_endpoint->UnregisterControlChangeNotify(m_volumeEvents.get());
        m_endpoint = nullptr;
    }
}

void AudioService::AddSession(IAudioSessionControl* raw)
{
    winrt::com_ptr<IAudioSessionControl> session;
    session.copy_from(raw);
    const auto control = session.try_as<IAudioSessionControl2>();
    const auto volume = session.try_as<ISimpleAudioVolume>();
    if (!control || !volume)
    {
        return;
    }
    for (const Session& known : m_sessions)
    {
        if (known.control == control)
        {
            return;
        }
    }
    AudioSessionState state = AudioSessionStateExpired;
    control->GetState(&state);
    DWORD processId = 0;
    control->GetProcessId(&processId);
    if (state == AudioSessionStateExpired || processId == GetCurrentProcessId())
    {
        return;
    }

    Session entry;
    entry.control = control;
    entry.volume = volume;
    entry.info.system = control->IsSystemSoundsSession() == S_OK;
    const std::wstring exe = processId ? ExeName(processId) : L"";
    std::wstring display;
    LPWSTR name = nullptr;
    if (SUCCEEDED(control->GetDisplayName(&name)) && name)
    {
        display = name;
        CoTaskMemFree(name);
    }
    if (display.starts_with(L"@"))
    {
        // A name stored in a DLL's resources, e.g. "@%SystemRoot%\...,-202".
        wchar_t resolved[256]{};
        display = SUCCEEDED(SHLoadIndirectString(display.c_str(), resolved, ARRAYSIZE(resolved), nullptr)) ? resolved : L"";
    }
    if (entry.info.system)
    {
        entry.info.key = L"system";
        entry.info.name = L"System sounds";
    }
    else
    {
        entry.info.key = !exe.empty() ? Lower(exe) : Lower(display);
        entry.info.name = !exe.empty() ? FriendlyExeName(exe) : display;
    }
    if (entry.info.key.empty())
    {
        return;  // nothing to call it by
    }
    BOOL muted = FALSE;
    volume->GetMasterVolume(&entry.info.volume);
    volume->GetMute(&muted);
    entry.info.muted = muted != FALSE;
    entry.info.active = state == AudioSessionStateActive;

    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    const GUID own = m_ownChanges;
    auto events = winrt::make_self<SessionEvents>();
    events->onChange = [queue, alive, this, own, control](LPCGUID context)
    {
        if (context && *context == own)
        {
            return;  // the echo of our own change
        }
        queue.TryEnqueue([alive, this, control]
        {
            if (!alive.expired())
            {
                OnSessionChanged(control.get());
            }
        });
    };
    entry.events = events.as<IAudioSessionEvents>();
    control->RegisterAudioSessionNotification(entry.events.get());
    m_sessions.push_back(std::move(entry));
}

void AudioService::OnSessionChanged(IAudioSessionControl2* control)
{
    const auto found = std::find_if(m_sessions.begin(), m_sessions.end(),
        [&](const Session& session) { return session.control.get() == control; });
    if (found == m_sessions.end())
    {
        return;
    }
    AudioSessionState state = AudioSessionStateExpired;
    found->control->GetState(&state);
    if (state == AudioSessionStateExpired)
    {
        // The app is gone.
        found->control->UnregisterAudioSessionNotification(found->events.get());
        m_sessions.erase(found);
    }
    else
    {
        BOOL muted = FALSE;
        found->volume->GetMasterVolume(&found->info.volume);
        found->volume->GetMute(&muted);
        found->info.muted = muted != FALSE;
        found->info.active = state == AudioSessionStateActive;
    }
    UpdateApps();
    Publish();
}

void AudioService::UpdateApps()
{
    std::vector<SessionInfo> sessions;
    for (const Session& session : m_sessions)
    {
        sessions.push_back(session.info);
    }
    m_state.apps = GroupSessions(sessions);
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

void AudioService::SetAppVolume(const std::wstring& key, float volume)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    for (Session& session : m_sessions)
    {
        if (session.info.key == key)
        {
            session.volume->SetMasterVolume(volume, &m_ownChanges);
            session.info.volume = volume;
            if (session.info.muted && volume > 0.0f)
            {
                session.volume->SetMute(FALSE, &m_ownChanges);
                session.info.muted = false;
            }
        }
    }
    UpdateApps();
    Publish();
}

void AudioService::ToggleAppMute(const std::wstring& key)
{
    const auto app = std::find_if(m_state.apps.begin(), m_state.apps.end(), [&](const AppVolume& entry) { return entry.key == key; });
    if (app == m_state.apps.end())
    {
        return;
    }
    const bool mute = !app->muted;
    for (Session& session : m_sessions)
    {
        if (session.info.key == key)
        {
            session.volume->SetMute(mute, &m_ownChanges);
            session.info.muted = mute;
        }
    }
    UpdateApps();
    Publish();
}

void AudioService::ToggleMicMute()
{
    if (!m_mic)
    {
        return;
    }
    const bool mute = !m_state.micMuted;
    winrt::com_ptr<IMMDeviceCollection> mics;
    if (SUCCEEDED(m_devices->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, mics.put())))
    {
        UINT count = 0;
        mics->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            winrt::com_ptr<IMMDevice> device;
            winrt::com_ptr<IAudioEndpointVolume> volume;
            if (SUCCEEDED(mics->Item(i, device.put())) &&
                SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, volume.put_void())))
            {
                volume->SetMute(mute, &m_ownChanges);
            }
        }
    }
    m_state.micMuted = mute;
    Publish();
}

void AudioService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
