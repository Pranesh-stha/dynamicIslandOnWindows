#include "services/BluetoothService.h"

#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>  // needs mmdeviceapi.h first
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cwctype>

#include "platform/Log.h"

namespace enumeration = winrt::Windows::Devices::Enumeration;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IPropertyValue;
using winrt::Windows::Foundation::PropertyType;

namespace
{
    // Device properties Windows keeps for Bluetooth devices.
    constexpr wchar_t kIsConnected[] = L"System.Devices.Aep.IsConnected";
    constexpr wchar_t kContainerId[] = L"System.Devices.Aep.ContainerId";
    constexpr wchar_t kCodMajor[] = L"System.Devices.Aep.Bluetooth.Cod.Major";
    constexpr wchar_t kCodMinor[] = L"System.Devices.Aep.Bluetooth.Cod.Minor";

    // How long a connect or disconnect may take before we stop showing "Connecting…".
    constexpr auto kToggleTimeout = std::chrono::seconds(12);

    // The user-mode view of IKsControl (ksproxy.h): sends "kernel streaming"
    // properties to an audio driver. Only KsProperty is used.
    MIDL_INTERFACE("28F54685-06FD-11D2-B27A-00A0C9223196")
    IKsControlUser : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE KsProperty(void* property, ULONG propertyLength, void* data, ULONG dataLength, ULONG* returned) = 0;
        virtual HRESULT STDMETHODCALLTYPE KsMethod(void* method, ULONG methodLength, void* data, ULONG dataLength, ULONG* returned) = 0;
        virtual HRESULT STDMETHODCALLTYPE KsEvent(void* event, ULONG eventLength, void* data, ULONG dataLength, ULONG* returned) = 0;
    };

    // KSPROPSETID_BtAudio and its two properties, from ksmedia.h: asking a
    // Bluetooth audio driver to reconnect or disconnect its device. This is
    // how Windows' own "Connect" button works for headphones and speakers.
    constexpr GUID kBtAudioPropertySet{ 0x7FA06C40, 0xB8F6, 0x4C7E, { 0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D } };
    constexpr ULONG kOneShotReconnect = 0;
    constexpr ULONG kOneShotDisconnect = 1;
    constexpr ULONG kPropertyGet = 0x00000001;
    struct KsProperty
    {
        GUID set;
        ULONG id;
        ULONG flags;
    };

    std::wstring Query(bool paired)
    {
        // Classic Bluetooth and Bluetooth Low Energy devices.
        return std::wstring(L"(System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\" OR "
                            L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\") AND "
                            L"System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#") + (paired ? L"True" : L"False");
    }

    IInspectable Property(const enumeration::DeviceInformation& info, const wchar_t* name)
    {
        const auto properties = info.Properties();
        return properties && properties.HasKey(name) ? properties.Lookup(name) : nullptr;
    }

    bool BoolProperty(const enumeration::DeviceInformation& info, const wchar_t* name)
    {
        return winrt::unbox_value_or<bool>(Property(info, name), false);
    }

    uint32_t NumberProperty(const enumeration::DeviceInformation& info, const wchar_t* name)
    {
        const auto value = Property(info, name).try_as<IPropertyValue>();
        if (!value)
        {
            return 0;
        }
        switch (value.Type())
        {
        case PropertyType::UInt8: return value.GetUInt8();
        case PropertyType::UInt16: return value.GetUInt16();
        case PropertyType::UInt32: return value.GetUInt32();
        default: return 0;
        }
    }

    winrt::guid ContainerOf(const enumeration::DeviceInformation& info)
    {
        return winrt::unbox_value_or<winrt::guid>(Property(info, kContainerId), winrt::guid{});
    }

    std::wstring GuidText(const GUID& guid)
    {
        wchar_t text[64]{};
        StringFromGUID2(guid, text, ARRAYSIZE(text));
        std::wstring result(text);
        std::transform(result.begin(), result.end(), result.begin(), ::towlower);
        return result;
    }

    std::wstring GuidText(const winrt::guid& guid)
    {
        return GuidText(reinterpret_cast<const GUID&>(guid));
    }

    // Every audio endpoint (speaker, headphone, microphone) Windows knows,
    // connected or not, with its device container (the physical device).
    template <typename F>
    void ForEachAudioEndpoint(F&& visit)
    {
        winrt::com_ptr<IMMDeviceEnumerator> devices;
        winrt::com_ptr<IMMDeviceCollection> endpoints;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices.put()))) ||
            FAILED(devices->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, endpoints.put())))
        {
            return;
        }
        UINT count = 0;
        endpoints->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            winrt::com_ptr<IMMDevice> endpoint;
            winrt::com_ptr<IPropertyStore> properties;
            if (FAILED(endpoints->Item(i, endpoint.put())) || FAILED(endpoint->OpenPropertyStore(STGM_READ, properties.put())))
            {
                continue;
            }
            PROPVARIANT container;
            PropVariantInit(&container);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_ContainerId, &container)) && container.vt == VT_CLSID && container.puuid)
            {
                visit(devices.get(), endpoint.get(), *container.puuid);
            }
            PropVariantClear(&container);
        }
    }

    // Sends "reconnect" or "disconnect" to the audio driver behind every
    // endpoint of one device. Runs on a background thread (drivers can be slow).
    bool SendBluetoothAudioCommand(const GUID& container, bool connect)
    {
        bool sent = false;
        std::set<std::wstring> filtersDone;
        ForEachAudioEndpoint([&](IMMDeviceEnumerator* devices, IMMDevice* endpoint, const GUID& endpointContainer)
        {
            if (!IsEqualGUID(endpointContainer, container))
            {
                return;
            }
            // Endpoint -> its connector -> the driver's filter on the other side.
            winrt::com_ptr<IDeviceTopology> topology, filterTopology;
            winrt::com_ptr<IConnector> connector, otherSide;
            winrt::com_ptr<IPart> part;
            LPWSTR filterId = nullptr;
            if (FAILED(endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_INPROC_SERVER, nullptr, topology.put_void())) ||
                FAILED(topology->GetConnector(0, connector.put())) || FAILED(connector->GetConnectedTo(otherSide.put())) ||
                !(part = otherSide.try_as<IPart>()) || FAILED(part->GetTopologyObject(filterTopology.put())) ||
                FAILED(filterTopology->GetDeviceId(&filterId)))
            {
                return;
            }
            const std::wstring id(filterId);
            CoTaskMemFree(filterId);
            if (!filtersDone.insert(id).second)
            {
                return;  // headsets have several endpoints on one filter
            }
            winrt::com_ptr<IMMDevice> filter;
            winrt::com_ptr<IKsControlUser> control;
            if (FAILED(devices->GetDevice(id.c_str(), filter.put())) ||
                FAILED(filter->Activate(__uuidof(IKsControlUser), CLSCTX_INPROC_SERVER, nullptr, control.put_void())))
            {
                return;
            }
            KsProperty property{ kBtAudioPropertySet, connect ? kOneShotReconnect : kOneShotDisconnect, kPropertyGet };
            ULONG returned = 0;
            if (SUCCEEDED(control->KsProperty(&property, sizeof(property), nullptr, 0, &returned)))
            {
                sent = true;
            }
        });
        return sent;
    }
}

DeviceKind KindFromClassOfDevice(uint32_t major, uint32_t minor)
{
    switch (major)
    {
    case 1: return DeviceKind::Computer;
    case 2: return DeviceKind::Phone;
    case 4: return DeviceKind::Audio;
    case 5:  // peripheral: the top two bits say keyboard / pointer, the rest the device type
    {
        const uint32_t input = (minor >> 4) & 0x3;
        if (input == 1 || input == 3) return DeviceKind::Keyboard;
        if (input == 2) return DeviceKind::Mouse;
        const uint32_t type = minor & 0xF;
        if (type == 1 || type == 2) return DeviceKind::Gamepad;
        return DeviceKind::Other;
    }
    default: return DeviceKind::Other;
    }
}

void SortDevices(std::vector<BluetoothDevice>& devices)
{
    std::stable_sort(devices.begin(), devices.end(), [](const BluetoothDevice& a, const BluetoothDevice& b)
    {
        if (a.connected != b.connected) return a.connected;
        if (a.canConnect != b.canConnect) return a.canConnect;  // headphones and speakers: the ones Island can connect
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

BluetoothService::~BluetoothService()
{
    m_alive.reset();
    StopSearch();
    if (m_pairedWatcher)
    {
        try
        {
            if (m_pairedWatcher.Status() == enumeration::DeviceWatcherStatus::Started ||
                m_pairedWatcher.Status() == enumeration::DeviceWatcherStatus::EnumerationCompleted)
            {
                m_pairedWatcher.Stop();
            }
        }
        catch (const winrt::hresult_error&)
        {
        }
    }
    if (m_mtaCookie)
    {
        CoDecrementMTAUsage(m_mtaCookie);
    }
}

void BluetoothService::Start(std::function<void(const BluetoothState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    CoIncrementMTAUsage(&m_mtaCookie);  // audio lookups run on thread-pool threads

    try
    {
        m_pairedWatcher = CreateWatcher(true);
        Subscribe(m_pairedWatcher, m_paired);
        m_pairedWatcher.Start();
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"bluetooth: can't list devices ({})", error.message());
    }
}

BluetoothService::Watcher BluetoothService::CreateWatcher(bool paired)
{
    auto properties = winrt::single_threaded_vector<winrt::hstring>({ kIsConnected, kContainerId, kCodMajor, kCodMinor });
    return enumeration::DeviceInformation::CreateWatcher(Query(paired), properties, enumeration::DeviceInformationKind::AssociationEndpoint);
}

void BluetoothService::Subscribe(Watcher& watcher, std::map<std::wstring, Entry>& entries)
{
    // The watcher reports from a background thread; everything is applied
    // on the UI thread, and only while this service still exists.
    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    const bool paired = &entries == &m_paired;
    auto& tokens = paired ? m_pairedTokens : m_searchTokens;
    auto* map = &entries;

    tokens.push_back(watcher.Added([queue, alive, this, map, paired](auto&&, const enumeration::DeviceInformation& info)
    {
        queue.TryEnqueue([alive, this, map, paired, info]
        {
            if (alive.expired())
            {
                return;
            }
            (*map)[std::wstring(info.Id())].info = info;
            if (paired)
            {
                RefreshAudioDevices();
            }
            Publish();
        });
    }));
    if (paired)
    {
        // Windows can take a while (30 s on some PCs) to say the list is complete;
        // devices show up one by one before that.
        tokens.push_back(watcher.EnumerationCompleted([queue, alive, this](auto&&, auto&&)
        {
            queue.TryEnqueue([alive, this]
            {
                if (!alive.expired())
                {
                    Log::Info(L"bluetooth: paired devices: {}", m_paired.size());
                }
            });
        }));
    }
    tokens.push_back(watcher.Updated([queue, alive, this, map](auto&&, const enumeration::DeviceInformationUpdate& update)
    {
        queue.TryEnqueue([alive, this, map, update]
        {
            if (alive.expired())
            {
                return;
            }
            const auto found = map->find(std::wstring(update.Id()));
            if (found == map->end())
            {
                return;
            }
            Entry& entry = found->second;
            const bool wasConnected = BoolProperty(entry.info, kIsConnected);
            entry.info.Update(update);
            if (entry.busy && BoolProperty(entry.info, kIsConnected) != wasConnected)
            {
                entry.busy = false;  // the connect or disconnect we asked for happened
                entry.detail.clear();
            }
            Publish();
        });
    }));
    tokens.push_back(watcher.Removed([queue, alive, this, map, paired](auto&&, const enumeration::DeviceInformationUpdate& update)
    {
        queue.TryEnqueue([alive, this, map, paired, update]
        {
            if (!alive.expired())
            {
                map->erase(std::wstring(update.Id()));
                if (paired)
                {
                    RefreshAudioDevices();
                }
                Publish();
            }
        });
    }));
}

winrt::fire_and_forget BluetoothService::RefreshAudioDevices()
{
    // Devices arrive one by one: one lookup at a time, plus one more at the
    // end if anything changed meanwhile.
    if (m_audioLookupRunning)
    {
        m_audioLookupAgain = true;
        co_return;
    }
    m_audioLookupRunning = true;
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    co_await winrt::resume_background();
    std::set<std::wstring> containers;
    ForEachAudioEndpoint([&](IMMDeviceEnumerator*, IMMDevice*, const GUID& container) { containers.insert(GuidText(container)); });
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    m_audioLookupRunning = false;
    m_audioContainers = std::move(containers);
    Publish();
    if (m_audioLookupAgain)
    {
        m_audioLookupAgain = false;
        RefreshAudioDevices();
    }
}

BluetoothDevice BluetoothService::Describe(const Entry& entry, bool paired) const
{
    BluetoothDevice device;
    const auto& info = entry.info;
    device.id = info.Id();
    device.name = info.Name();
    device.connected = paired && BoolProperty(info, kIsConnected);
    device.kind = KindFromClassOfDevice(NumberProperty(info, kCodMajor), NumberProperty(info, kCodMinor));
    device.canConnect = paired && m_audioContainers.contains(GuidText(ContainerOf(info)));
    if (device.canConnect && device.kind == DeviceKind::Other)
    {
        device.kind = DeviceKind::Audio;
    }
    device.busy = entry.busy;
    if (!entry.detail.empty())
    {
        device.detail = entry.detail;
    }
    else if (!paired)
    {
        device.detail = L"Click to pair";
    }
    else
    {
        device.detail = device.connected ? L"Connected" : device.canConnect ? L"Not connected" : L"Paired";
    }
    return device;
}

void BluetoothService::Publish()
{
    BluetoothState state;
    state.searching = m_searching;

    // A device can be listed twice (once as classic Bluetooth, once as Low
    // Energy); both belong to one physical device, its "container".
    std::map<std::wstring, size_t> byContainer;
    for (const auto& [id, entry] : m_paired)
    {
        BluetoothDevice device = Describe(entry, true);
        const std::wstring container = GuidText(ContainerOf(entry.info));
        if (const auto same = byContainer.find(container); same != byContainer.end() && container != GuidText(GUID{}))
        {
            BluetoothDevice& first = state.paired[same->second];
            first.connected = first.connected || device.connected;
            first.canConnect = first.canConnect || device.canConnect;
            first.busy = first.busy || device.busy;
            if (first.connected)
            {
                first.detail = device.busy ? device.detail : L"Connected";
            }
            continue;
        }
        byContainer[container] = state.paired.size();
        state.paired.push_back(std::move(device));
    }
    SortDevices(state.paired);

    for (const auto& [id, entry] : m_nearby)
    {
        BluetoothDevice device = Describe(entry, false);
        if (!device.name.empty())  // unnamed beacons and the like aren't something you'd pair
        {
            state.nearby.push_back(std::move(device));
        }
    }
    SortDevices(state.nearby);

    if (m_onChange)
    {
        m_onChange(state);
    }
}

void BluetoothService::Toggle(const std::wstring& id)
{
    const auto found = m_paired.find(id);
    if (found == m_paired.end() || found->second.busy)
    {
        return;
    }
    const auto& info = found->second.info;
    const winrt::guid container = ContainerOf(info);
    if (!m_audioContainers.contains(GuidText(container)))
    {
        return;  // only audio devices can be connected from here
    }
    ToggleAsync(id, container, !BoolProperty(info, kIsConnected));
}

winrt::fire_and_forget BluetoothService::ToggleAsync(std::wstring id, winrt::guid container, bool connect)
{
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    {
        Entry& entry = m_paired[id];
        entry.busy = true;
        entry.detail = connect ? L"Connecting…" : L"Disconnecting…";
    }
    // Every paired entry of the same device shows it too.
    Publish();

    co_await winrt::resume_background();
    const bool sent = SendBluetoothAudioCommand(reinterpret_cast<const GUID&>(container), connect);
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    if (!sent)
    {
        Log::Info(L"bluetooth: the device's driver didn't take the {} request", connect ? L"connect" : L"disconnect");
        if (const auto found = m_paired.find(id); found != m_paired.end())
        {
            found->second.busy = false;
            found->second.detail = connect ? L"Couldn't connect" : L"";
        }
        Publish();
        co_return;
    }

    // The device watcher reports the change when it happens. If nothing
    // comes, stop showing "Connecting…" after a while.
    co_await winrt::resume_after(kToggleTimeout);
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    if (const auto found = m_paired.find(id); found != m_paired.end() && found->second.busy)
    {
        found->second.busy = false;
        found->second.detail = connect ? L"Couldn't connect (is it on and nearby?)" : L"";
        Publish();
    }
}

void BluetoothService::StartSearch()
{
    if (m_searchWatcher)
    {
        return;
    }
    m_nearby.clear();
    try
    {
        m_searchWatcher = CreateWatcher(false);
        Subscribe(m_searchWatcher, m_nearby);
        m_searchWatcher.Start();
        m_searching = true;
        Log::Info(L"bluetooth: searching for new devices");
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"bluetooth: can't search ({})", error.message());
        m_searchWatcher = nullptr;
    }
    Publish();
}

void BluetoothService::StopSearch()
{
    if (!m_searchWatcher)
    {
        return;
    }
    try
    {
        m_searchWatcher.Added(m_searchTokens.at(0));
        m_searchWatcher.Updated(m_searchTokens.at(1));
        m_searchWatcher.Removed(m_searchTokens.at(2));
        const auto status = m_searchWatcher.Status();
        if (status == enumeration::DeviceWatcherStatus::Started || status == enumeration::DeviceWatcherStatus::EnumerationCompleted)
        {
            m_searchWatcher.Stop();
        }
    }
    catch (...)
    {
    }
    m_searchTokens.clear();
    m_searchWatcher = nullptr;
    m_searching = false;
    // Keep devices that are being paired; forget the rest.
    std::erase_if(m_nearby, [](const auto& item) { return !item.second.busy; });
    if (m_alive)
    {
        Publish();
    }
}

void BluetoothService::Pair(const std::wstring& id)
{
    const auto found = m_nearby.find(id);
    if (found != m_nearby.end() && !found->second.busy)
    {
        PairAsync(id);
    }
}

winrt::fire_and_forget BluetoothService::PairAsync(std::wstring id)
{
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    Entry& entry = m_nearby[id];
    entry.busy = true;
    entry.detail = L"Pairing…";
    const auto info = entry.info;
    Publish();

    using enumeration::DevicePairingKinds;
    bool paired = false;
    bool needsSettings = false;
    try
    {
        // Pairing asks how to prove it's the right device. We handle "just
        // confirm" and the kinds where a code is shown (you type it on the
        // keyboard, or check it matches); typing a device's code needs Windows.
        auto custom = info.Pairing().Custom();
        const auto token = custom.PairingRequested([queue, alive, this, id](auto&&, const enumeration::DevicePairingRequestedEventArgs& request)
        {
            std::wstring message;
            if (request.PairingKind() == DevicePairingKinds::DisplayPin)
            {
                message = L"Type " + std::wstring(request.Pin()) + L" on it, then Enter";
            }
            else if (request.PairingKind() == DevicePairingKinds::ConfirmPinMatch)
            {
                message = L"Code " + std::wstring(request.Pin());
            }
            request.Accept();
            if (!message.empty())
            {
                queue.TryEnqueue([alive, this, id, message]
                {
                    if (!alive.expired())
                    {
                        if (const auto found = m_nearby.find(id); found != m_nearby.end())
                        {
                            found->second.detail = message;
                        }
                        Publish();
                    }
                });
            }
        });
        const auto result = co_await custom.PairAsync(
            DevicePairingKinds::ConfirmOnly | DevicePairingKinds::DisplayPin | DevicePairingKinds::ConfirmPinMatch,
            enumeration::DevicePairingProtectionLevel::Default);
        custom.PairingRequested(token);
        const auto status = result.Status();
        paired = status == enumeration::DevicePairingResultStatus::Paired || status == enumeration::DevicePairingResultStatus::AlreadyPaired;
        needsSettings = status == enumeration::DevicePairingResultStatus::PairingCanceled ||
                        status == enumeration::DevicePairingResultStatus::RequiredHandlerNotRegistered;
        if (!paired)
        {
            Log::Info(L"bluetooth: pairing failed ({})", static_cast<int>(status));
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"bluetooth: pairing failed ({})", error.message());
    }
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    if (paired)
    {
        m_nearby.erase(id);  // it shows up in the paired list now
        Log::Info(L"bluetooth: paired a new device");
    }
    else if (const auto found = m_nearby.find(id); found != m_nearby.end())
    {
        found->second.busy = false;
        found->second.detail = L"Couldn't pair";
    }
    if (needsSettings)
    {
        // This device wants a code typed in: Windows' own pairing screen can do that.
        ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
    }
    Publish();
}
