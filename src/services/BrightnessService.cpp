#include "services/BrightnessService.h"

#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <WbemIdl.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "platform/Log.h"

namespace
{
    constexpr BYTE kLuminance = 0x10;     // the DDC/CI setting number for brightness
    constexpr int64_t kKeepOwnValueMs = 2000;  // after a write, monitors may briefly report the old value

    int64_t NowMs()
    {
        return static_cast<int64_t>(GetTickCount64());
    }

    // A BSTR is the string type WMI wants; this frees it automatically.
    struct Bstr
    {
        explicit Bstr(const wchar_t* text) : value(SysAllocString(text)) {}
        ~Bstr() { SysFreeString(value); }
        Bstr(const Bstr&) = delete;
        Bstr& operator=(const Bstr&) = delete;
        BSTR value;
    };

    bool IsBuiltIn(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology)
    {
        return technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
               technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS ||
               technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
               technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
    }

    struct MonitorLookup
    {
        const wchar_t* gdiName;
        HMONITOR found = nullptr;
        bool primary = false;
    };

    BOOL CALLBACK MatchMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
    {
        auto* lookup = reinterpret_cast<MonitorLookup*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) && wcscmp(info.szDevice, lookup->gdiName) == 0)
        {
            lookup->found = monitor;
            lookup->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            return FALSE;  // stop looking
        }
        return TRUE;
    }
}

BrightnessService::~BrightnessService()
{
    m_alive.reset();
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void BrightnessService::Start(std::function<void(const BrightnessState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    m_refresh = true;  // read everything once at start
    m_thread = std::thread([this] { Run(); });
}

void BrightnessService::Refresh()
{
    {
        std::lock_guard lock(m_mutex);
        m_refresh = true;
    }
    m_wake.notify_one();
}

void BrightnessService::Set(const std::wstring& id, float value)
{
    {
        std::lock_guard lock(m_mutex);
        m_requests[id] = static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    }
    m_wake.notify_one();
}

void BrightnessService::Run()
{
    // WMI is a COM service; this thread joins COM's multi-threaded apartment.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    std::unique_lock lock(m_mutex);
    int64_t nextCheck = Throttle::kNever;
    while (true)
    {
        // Sleep until there's something to do (or a throttled write is due).
        auto hasWork = [&] { return m_stop || m_refresh || !m_requests.empty(); };
        if (nextCheck == Throttle::kNever)
        {
            m_wake.wait(lock, hasWork);
        }
        else
        {
            m_wake.wait_for(lock, std::chrono::milliseconds(std::max<int64_t>(0, nextCheck - NowMs())), hasWork);
        }
        if (m_stop)
        {
            break;
        }
        const bool refresh = std::exchange(m_refresh, false);
        const auto requests = std::exchange(m_requests, {});
        lock.unlock();

        if (refresh)
        {
            Enumerate();
            ReadAll();
            Publish();
        }
        for (const auto& [id, value] : requests)
        {
            for (Monitor& monitor : m_monitors)
            {
                if (monitor.info.id == id && monitor.info.supported)
                {
                    monitor.throttle.Want(value);
                }
            }
        }
        nextCheck = Throttle::kNever;
        for (Monitor& monitor : m_monitors)
        {
            int64_t next = Throttle::kNever;
            if (const auto value = monitor.throttle.Take(NowMs(), next); value && Write(monitor, *value))
            {
                monitor.info.value = *value;
            }
            nextCheck = std::min(nextCheck, next);
        }

        lock.lock();
    }

    CloseMonitors();
    m_wmi = nullptr;
    winrt::uninit_apartment();
}

void BrightnessService::Enumerate()
{
    // Keep what we know (values, recent writes) for displays that stay.
    std::vector<Monitor> previous = std::move(m_monitors);
    m_monitors.clear();

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
    {
        return;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
    {
        return;
    }
    paths.resize(pathCount);

    for (const DISPLAYCONFIG_PATH_INFO& path : paths)
    {
        // The Windows name of the screen ("\\.\DISPLAY2") and the monitor's own name.
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id };
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id };
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
        {
            continue;
        }

        Monitor monitor;
        monitor.info.id = std::wstring(source.viewGdiDeviceName) + L"#" + std::to_wstring(path.targetInfo.id);
        monitor.info.internal = IsBuiltIn(target.outputTechnology);
        monitor.info.name = monitor.info.internal ? L"Built-in display"
                          : target.monitorFriendlyDeviceName[0] ? target.monitorFriendlyDeviceName : L"Display";
        // DDC/CI writes are stored in the monitor's own memory, so they're
        // spaced out more than the laptop panel's.
        monitor.throttle = Throttle(monitor.info.internal ? 100 : 250);

        MonitorLookup lookup{ source.viewGdiDeviceName };
        EnumDisplayMonitors(nullptr, nullptr, MatchMonitor, reinterpret_cast<LPARAM>(&lookup));
        monitor.info.primary = lookup.primary;
        if (!monitor.info.internal && lookup.found)
        {
            DWORD count = 0;
            if (GetNumberOfPhysicalMonitorsFromHMONITOR(lookup.found, &count) && count > 0)
            {
                std::vector<PHYSICAL_MONITOR> physical(count);
                if (GetPhysicalMonitorsFromHMONITOR(lookup.found, count, physical.data()))
                {
                    monitor.physical = physical[0].hPhysicalMonitor;
                    for (DWORD i = 1; i < count; ++i)
                    {
                        DestroyPhysicalMonitor(physical[i].hPhysicalMonitor);
                    }
                }
            }
        }
        for (const Monitor& old : previous)
        {
            if (old.info.id == monitor.info.id)
            {
                monitor.info.value = old.info.value;
                monitor.info.supported = old.info.supported;
                monitor.throttle = old.throttle;
                monitor.readOnce = old.readOnce;
            }
        }
        m_monitors.push_back(std::move(monitor));
    }
    for (Monitor& old : previous)
    {
        if (old.physical)
        {
            DestroyPhysicalMonitor(old.physical);
        }
    }
    std::stable_sort(m_monitors.begin(), m_monitors.end(),
        [](const Monitor& a, const Monitor& b) { return a.info.primary && !b.info.primary; });
}

void BrightnessService::ReadAll()
{
    for (Monitor& monitor : m_monitors)
    {
        // Right after our own write, keep our value (the monitor may still report the old one).
        if (monitor.throttle.LastWrite() != Throttle::kNever && NowMs() - monitor.throttle.LastWrite() < kKeepOwnValueMs)
        {
            continue;
        }
        int value = -1;
        if (monitor.info.internal)
        {
            value = ConnectWmi() ? ReadWmi() : -1;
        }
        else if (monitor.physical)
        {
            // Monitors sometimes miss a request; try twice.
            for (int attempt = 0; attempt < 2 && value < 0; ++attempt)
            {
                MC_VCP_CODE_TYPE type{};
                DWORD current = 0, maximum = 0;
                if (GetVCPFeatureAndVCPFeatureReply(monitor.physical, kLuminance, &type, &current, &maximum) && maximum > 0)
                {
                    monitor.maximum = maximum;
                    value = static_cast<int>(std::lround(100.0 * current / maximum));
                }
                else if (attempt == 0)
                {
                    Sleep(40);
                }
            }
        }
        const bool wasSupported = monitor.info.supported;
        monitor.info.supported = value >= 0;
        if (value >= 0)
        {
            monitor.info.value = value;
            monitor.throttle.Known(value);
        }
        if (!monitor.readOnce || monitor.info.supported != wasSupported)
        {
            monitor.readOnce = true;
            Log::Info(L"brightness: {} {}", monitor.info.name,
                value >= 0 ? std::format(L"at {} %", value) : std::wstring(L"can't be controlled"));
        }
    }
}

bool BrightnessService::Write(Monitor& monitor, int value)
{
    if (monitor.info.internal)
    {
        return ConnectWmi() && WriteWmi(value);
    }
    if (!monitor.physical)
    {
        return false;
    }
    const DWORD raw = static_cast<DWORD>(std::lround(value / 100.0 * monitor.maximum));
    if (SetVCPFeature(monitor.physical, kLuminance, raw))
    {
        return true;
    }
    Sleep(40);
    return SetVCPFeature(monitor.physical, kLuminance, raw) != FALSE;
}

void BrightnessService::Publish()
{
    BrightnessState state;
    for (const Monitor& monitor : m_monitors)
    {
        state.displays.push_back(monitor.info);
    }
    const std::weak_ptr<bool> alive = m_alive;
    m_queue.TryEnqueue([alive, this, state = std::move(state)]
    {
        if (!alive.expired() && m_onChange)
        {
            m_onChange(state);
        }
    });
}

void BrightnessService::CloseMonitors()
{
    for (Monitor& monitor : m_monitors)
    {
        if (monitor.physical)
        {
            DestroyPhysicalMonitor(monitor.physical);
            monitor.physical = nullptr;
        }
    }
}

// --- The laptop panel, through WMI -------------------------------------------

bool BrightnessService::ConnectWmi()
{
    if (m_wmi)
    {
        return true;
    }
    winrt::com_ptr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.put()))))
    {
        return false;
    }
    const Bstr root(L"ROOT\\WMI");
    if (FAILED(locator->ConnectServer(root.value, nullptr, nullptr, nullptr, 0, nullptr, nullptr, m_wmi.put())))
    {
        Log::Info(L"brightness: can't connect to WMI");
        return false;
    }
    CoSetProxyBlanket(m_wmi.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return true;
}

int BrightnessService::ReadWmi()
{
    const Bstr language(L"WQL");
    const Bstr query(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active = TRUE");
    winrt::com_ptr<IEnumWbemClassObject> results;
    if (FAILED(m_wmi->ExecQuery(language.value, query.value, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, results.put())))
    {
        return -1;
    }
    winrt::com_ptr<IWbemClassObject> panel;
    ULONG count = 0;
    if (FAILED(results->Next(5000, 1, panel.put(), &count)) || count == 0)
    {
        return -1;
    }
    VARIANT value;
    VariantInit(&value);
    int brightness = -1;
    if (SUCCEEDED(panel->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr)))
    {
        brightness = value.vt == VT_UI1 ? value.bVal : value.vt == VT_I4 ? static_cast<int>(value.lVal) : -1;
    }
    VariantClear(&value);
    return brightness;
}

bool BrightnessService::WriteWmi(int value)
{
    // The panel's "methods" object has a WmiSetBrightness(Timeout, Brightness) method.
    if (m_wmiMethodsPath.empty())
    {
        const Bstr language(L"WQL");
        const Bstr query(L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active = TRUE");
        winrt::com_ptr<IEnumWbemClassObject> results;
        winrt::com_ptr<IWbemClassObject> methods;
        ULONG count = 0;
        if (FAILED(m_wmi->ExecQuery(language.value, query.value, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, results.put())) ||
            FAILED(results->Next(5000, 1, methods.put(), &count)) || count == 0)
        {
            return false;
        }
        VARIANT path;
        VariantInit(&path);
        if (SUCCEEDED(methods->Get(L"__PATH", 0, &path, nullptr, nullptr)) && path.vt == VT_BSTR)
        {
            m_wmiMethodsPath = path.bstrVal;
        }
        VariantClear(&path);
        if (m_wmiMethodsPath.empty())
        {
            return false;
        }
    }

    const Bstr className(L"WmiMonitorBrightnessMethods");
    const Bstr methodName(L"WmiSetBrightness");
    winrt::com_ptr<IWbemClassObject> methodClass, signature, arguments;
    if (FAILED(m_wmi->GetObject(className.value, 0, nullptr, methodClass.put(), nullptr)) ||
        FAILED(methodClass->GetMethod(methodName.value, 0, signature.put(), nullptr)) ||
        FAILED(signature->SpawnInstance(0, arguments.put())))
    {
        return false;
    }
    VARIANT timeout;
    VariantInit(&timeout);
    timeout.vt = VT_I4;
    timeout.lVal = 0;
    VARIANT brightness;
    VariantInit(&brightness);
    brightness.vt = VT_UI1;
    brightness.bVal = static_cast<BYTE>(value);
    arguments->Put(L"Timeout", 0, &timeout, 0);
    arguments->Put(L"Brightness", 0, &brightness, 0);
    const Bstr path(m_wmiMethodsPath.c_str());
    return SUCCEEDED(m_wmi->ExecMethod(path.value, methodName.value, 0, nullptr, arguments.get(), nullptr, nullptr));
}
