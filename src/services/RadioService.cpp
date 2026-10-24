#include "services/RadioService.h"

#include <winrt/Windows.Networking.Connectivity.h>

#include "platform/Log.h"

namespace radios = winrt::Windows::Devices::Radios;
namespace connectivity = winrt::Windows::Networking::Connectivity;

RadioService::~RadioService()
{
    m_alive.reset();
    for (Entry* entry : { &m_wifi, &m_bluetooth })
    {
        if (entry->radio)
        {
            entry->radio.StateChanged(entry->token);
        }
    }
    if (m_networkToken)
    {
        connectivity::NetworkInformation::NetworkStatusChanged(m_networkToken);
    }
}

void RadioService::Start(std::function<void(const RadiosState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    m_wifi.wifi = true;
    StartAsync();
}

winrt::fire_and_forget RadioService::StartAsync()
{
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    try
    {
        const auto access = co_await radios::Radio::RequestAccessAsync();
        if (alive.expired())
        {
            co_return;
        }
        if (access != radios::RadioAccessStatus::Allowed)
        {
            Log::Info(L"radios: Windows doesn't let us switch radios ({})", static_cast<int>(access));
        }
        const auto all = co_await radios::Radio::GetRadiosAsync();
        if (alive.expired())
        {
            co_return;
        }
        for (const auto& radio : all)
        {
            Entry* entry = radio.Kind() == radios::RadioKind::WiFi ? &m_wifi
                         : radio.Kind() == radios::RadioKind::Bluetooth ? &m_bluetooth : nullptr;
            if (!entry || entry->radio)
            {
                continue;  // only the first radio of each kind
            }
            entry->radio = radio;
            // Raised on a background thread when the radio changes (from
            // anywhere: Quick Settings, airplane mode, us).
            entry->token = radio.StateChanged([queue, alive, this, entry](auto&&, auto&&)
            {
                queue.TryEnqueue([alive, this, entry]
                {
                    if (!alive.expired())
                    {
                        Read(*entry);
                        Publish();
                    }
                });
            });
        }
        Log::Info(L"radios: Wi-Fi {}, Bluetooth {}", m_wifi.radio ? L"found" : L"missing", m_bluetooth.radio ? L"found" : L"missing");
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"radios: unavailable ({})", error.message());
    }
    if (alive.expired())
    {
        co_return;
    }

    // The Wi-Fi network's name follows network changes.
    m_networkToken = connectivity::NetworkInformation::NetworkStatusChanged([queue, alive, this](auto&&)
    {
        queue.TryEnqueue([alive, this]
        {
            if (!alive.expired())
            {
                ReadNetworkName();
            }
        });
    });
    Read(m_wifi);
    Read(m_bluetooth);
    Publish();
    ReadNetworkName();
}

winrt::fire_and_forget RadioService::ReadNetworkName()
{
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    std::wstring name;
    bool connected = false;
    co_await winrt::resume_background();  // listing networks can take a moment
    try
    {
        if (const auto profiles = connectivity::NetworkInformation::GetConnectionProfiles())
        {
            for (const auto& profile : profiles)
            {
                if (profile && profile.IsWlanConnectionProfile() &&
                    profile.GetNetworkConnectivityLevel() != connectivity::NetworkConnectivityLevel::None)
                {
                    connected = true;
                    name = profile.ProfileName();
                    break;
                }
            }
        }
    }
    catch (const winrt::hresult_error&)
    {
        // Windows 11 can hide network names from apps without location
        // permission (PLAN.md E10); then the tile just says "Connected".
    }
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    m_wifiConnected = connected;
    m_networkName = name;
    Read(m_wifi);
    Publish();
}

void RadioService::Read(Entry& entry)
{
    RadioInfo& info = entry.wifi ? m_state.wifi : m_state.bluetooth;
    if (!entry.radio)
    {
        info = RadioInfo{};
        info.detail = L"Not available";
        return;
    }
    info.available = true;
    if (entry.inFlight)
    {
        return;  // keep showing what you clicked until Windows is done
    }
    const auto state = entry.radio.State();
    info.on = state == radios::RadioState::On;
    info.switchable = state != radios::RadioState::Disabled;
    info.busy = false;
    if (!info.switchable)
    {
        info.detail = L"Off in Windows";  // the real state, never a fake one (E11)
    }
    else if (!info.on)
    {
        info.detail = L"Off";
    }
    else if (entry.wifi)
    {
        info.detail = !m_wifiConnected ? L"Not connected" : m_networkName.empty() ? L"Connected" : m_networkName;
    }
    else
    {
        info.detail = L"On";
    }
}

void RadioService::Set(Entry& entry, bool on)
{
    if (!entry.radio)
    {
        return;
    }
    // Show it at once; Windows follows.
    entry.wanted = on;
    RadioInfo& info = entry.wifi ? m_state.wifi : m_state.bluetooth;
    info.on = on;
    info.busy = true;
    info.detail = on ? L"Turning on…" : L"Turning off…";
    Publish();
    if (!entry.inFlight)
    {
        Apply(entry);
    }
}

winrt::fire_and_forget RadioService::Apply(Entry& entry)
{
    entry.inFlight = true;
    const std::weak_ptr<bool> alive = m_alive;
    try
    {
        // Clicks that arrive while Windows is switching only change `wanted`;
        // afterwards we switch again if needed, so on/off/on becomes just "on".
        while (true)
        {
            const bool target = entry.wanted;
            const auto status = co_await entry.radio.SetStateAsync(target ? radios::RadioState::On : radios::RadioState::Off);
            if (alive.expired())
            {
                co_return;
            }
            if (status != radios::RadioAccessStatus::Allowed)
            {
                Log::Info(L"radios: Windows refused to switch {} ({})", entry.wifi ? L"Wi-Fi" : L"Bluetooth", static_cast<int>(status));
                break;
            }
            if (entry.wanted == target)
            {
                break;
            }
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"radios: switching failed ({})", error.message());
    }
    if (alive.expired())
    {
        co_return;
    }
    entry.inFlight = false;
    Read(entry);  // the real state: the toggle springs back if Windows refused
    Publish();
}

void RadioService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
