#include "services/WifiService.h"

#include <shellapi.h>

#include <winrt/Windows.Networking.Connectivity.h>

#include <algorithm>
#include <map>

#include "platform/Log.h"

namespace wifi = winrt::Windows::Devices::WiFi;
namespace connectivity = winrt::Windows::Networking::Connectivity;

namespace
{
    // Windows scans on its own too; asking more often than this only drains power.
    constexpr uint64_t kMinScanIntervalMs = 8000;

    bool IsSecured(const wifi::WiFiAvailableNetwork& network)
    {
        const auto type = network.SecuritySettings().NetworkAuthenticationType();
        using Type = winrt::Windows::Networking::Connectivity::NetworkAuthenticationType;
        return type != Type::Open80211 && type != Type::None && type != Type::Unknown;
    }
}

std::vector<WifiNetwork> GroupNetworks(const std::vector<WifiNetwork>& accessPoints, const std::wstring& connectedSsid,
                                       const std::vector<std::wstring>& savedSsids)
{
    std::map<std::wstring, WifiNetwork> byName;
    for (const WifiNetwork& point : accessPoints)
    {
        if (point.ssid.empty())
        {
            continue;  // a hidden network
        }
        auto [entry, isNew] = byName.try_emplace(point.ssid, point);
        if (!isNew)
        {
            entry->second.bars = std::max(entry->second.bars, point.bars);
            entry->second.secured = entry->second.secured || point.secured;
        }
    }
    std::vector<WifiNetwork> networks;
    for (auto& [name, network] : byName)
    {
        network.connected = !connectedSsid.empty() && name == connectedSsid;
        network.saved = std::find(savedSsids.begin(), savedSsids.end(), name) != savedSsids.end();
        networks.push_back(network);
    }
    // The connected network may be missing from a scan (Windows sometimes
    // skips it); it still belongs at the top.
    if (!connectedSsid.empty() && !byName.contains(connectedSsid))
    {
        networks.push_back({ connectedSsid, 4, true, true, true });
    }
    std::stable_sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b)
    {
        if (a.connected != b.connected) return a.connected;
        if (a.saved != b.saved) return a.saved;
        if (a.bars != b.bars) return a.bars > b.bars;
        return a.ssid < b.ssid;
    });
    return networks;
}

WifiService::~WifiService()
{
    m_alive.reset();
}

void WifiService::Start(std::function<void(const WifiState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    StartAsync();
}

winrt::fire_and_forget WifiService::StartAsync()
{
    const std::weak_ptr<bool> alive = m_alive;
    try
    {
        const auto access = co_await wifi::WiFiAdapter::RequestAccessAsync();
        if (alive.expired())
        {
            co_return;
        }
        if (access != wifi::WiFiAccessStatus::Allowed)
        {
            Log::Info(L"wifi: Windows doesn't let us list networks ({})", static_cast<int>(access));
            co_return;
        }
        const auto adapters = co_await wifi::WiFiAdapter::FindAllAdaptersAsync();
        if (alive.expired() || !adapters || adapters.Size() == 0)
        {
            Log::Info(L"wifi: no Wi-Fi adapter");
            co_return;
        }
        m_adapter = adapters.GetAt(0);
        m_state.available = true;
        co_await RefreshAsync();  // what Windows already knows, without a new scan
        if (!alive.expired())
        {
            Log::Info(L"wifi: networks in range: {}", m_state.networks.size());
            Publish();
            if (m_scanWanted)
            {
                m_scanWanted = false;
                Scan();
            }
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"wifi: unavailable ({})", error.message());
    }
}

void WifiService::Scan()
{
    if (!m_adapter)
    {
        m_scanWanted = true;  // StartAsync scans once it has the adapter
        return;
    }
    if (m_state.scanning || GetTickCount64() - m_lastScan < kMinScanIntervalMs)
    {
        return;
    }
    ScanAsync();
}

winrt::fire_and_forget WifiService::ScanAsync()
{
    const std::weak_ptr<bool> alive = m_alive;
    m_lastScan = GetTickCount64();
    m_state.scanning = true;
    Publish();
    try
    {
        co_await m_adapter.ScanAsync();  // a few seconds; the UI keeps running
    }
    catch (const winrt::hresult_error&)
    {
        // Wi-Fi is off, or a scan is already running: show what we have.
    }
    if (alive.expired())
    {
        co_return;
    }
    try
    {
        co_await RefreshAsync();
    }
    catch (const winrt::hresult_error&)
    {
    }
    if (alive.expired())
    {
        co_return;
    }
    m_state.scanning = false;
    Publish();
}

winrt::Windows::Foundation::IAsyncAction WifiService::RefreshAsync()
{
    const std::weak_ptr<bool> alive = m_alive;
    // The networks in range, from the last scan (Windows' or ours).
    std::vector<WifiNetwork> points;
    m_seen.clear();
    if (const auto report = m_adapter.NetworkReport())
    {
        for (const auto& network : report.AvailableNetworks())
        {
            m_seen.push_back(network);
            points.push_back({ std::wstring(network.Ssid()), static_cast<int>(network.SignalBars()), IsSecured(network) });
        }
    }

    // Which one we're on.
    std::wstring connected;
    if (const auto profile = co_await m_adapter.NetworkAdapter().GetConnectedProfileAsync())
    {
        if (profile.IsWlanConnectionProfile())
        {
            connected = profile.WlanConnectionProfileDetails().GetConnectedSsid();
        }
    }
    if (alive.expired())
    {
        co_return;
    }

    // Networks Windows has a password for.
    std::vector<std::wstring> saved;
    connectivity::ConnectionProfileFilter filter;
    filter.IsWlanConnectionProfile(true);
    if (const auto profiles = co_await connectivity::NetworkInformation::FindConnectionProfilesAsync(filter))
    {
        for (const auto& profile : profiles)
        {
            saved.emplace_back(profile.ProfileName());
        }
    }
    if (alive.expired())
    {
        co_return;
    }
    m_state.networks = GroupNetworks(points, connected, saved);
}

void WifiService::Connect(const std::wstring& ssid)
{
    if (m_adapter && m_state.connecting.empty())
    {
        ConnectAsync(ssid);
    }
}

winrt::fire_and_forget WifiService::ConnectAsync(std::wstring ssid)
{
    const std::weak_ptr<bool> alive = m_alive;
    // The strongest access point with that name.
    wifi::WiFiAvailableNetwork network{ nullptr };
    for (const auto& candidate : m_seen)
    {
        if (std::wstring(candidate.Ssid()) == ssid && (!network || candidate.SignalBars() > network.SignalBars()))
        {
            network = candidate;
        }
    }
    const auto known = std::find_if(m_state.networks.begin(), m_state.networks.end(),
        [&](const WifiNetwork& entry) { return entry.ssid == ssid; });
    const bool needsPassword = known != m_state.networks.end() && known->secured && !known->saved;
    if (!network || needsPassword)
    {
        // Typing a password needs keyboard focus, which the island never
        // takes: Windows' own network list asks for it instead.
        ShellExecuteW(nullptr, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL);
        co_return;
    }

    m_state.connecting = ssid;
    Publish();
    bool joined = false;
    try
    {
        const auto result = co_await m_adapter.ConnectAsync(network, wifi::WiFiReconnectionKind::Automatic);
        joined = result.ConnectionStatus() == wifi::WiFiConnectionStatus::Success;
        if (!joined)
        {
            Log::Info(L"wifi: couldn't join a network ({})", static_cast<int>(result.ConnectionStatus()));
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"wifi: joining failed ({})", error.message());
    }
    if (alive.expired())
    {
        co_return;
    }
    if (!joined)
    {
        // Wrong or missing password: let Windows ask for it.
        ShellExecuteW(nullptr, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL);
    }
    try
    {
        co_await RefreshAsync();
    }
    catch (const winrt::hresult_error&)
    {
    }
    if (alive.expired())
    {
        co_return;
    }
    m_state.connecting.clear();
    Publish();
}

void WifiService::Disconnect()
{
    if (!m_adapter)
    {
        return;
    }
    try
    {
        m_adapter.Disconnect();
    }
    catch (const winrt::hresult_error&)
    {
    }
    for (WifiNetwork& network : m_state.networks)
    {
        network.connected = false;
    }
    Publish();
}

void WifiService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
