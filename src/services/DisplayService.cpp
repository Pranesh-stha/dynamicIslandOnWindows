#include "services/DisplayService.h"

#include <cmath>
#include <set>
#include <utility>

#include "platform/Log.h"

namespace
{
    bool IsBuiltIn(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology)
    {
        return technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
               technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
               technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
    }

    // Asks Windows for its display paths: which outputs show which picture.
    bool Query(UINT32 flags, std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
               DISPLAYCONFIG_TOPOLOGY_ID* topology = nullptr)
    {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount) != ERROR_SUCCESS)
        {
            return false;
        }
        paths.resize(pathCount);
        modes.resize(modeCount);
        if (QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), topology) != ERROR_SUCCESS)
        {
            return false;
        }
        paths.resize(pathCount);
        modes.resize(modeCount);
        return true;
    }
}

Projection ProjectionFromTopology(UINT32 topology)
{
    switch (topology)
    {
    case DISPLAYCONFIG_TOPOLOGY_INTERNAL: return Projection::PcScreenOnly;
    case DISPLAYCONFIG_TOPOLOGY_CLONE: return Projection::Duplicate;
    case DISPLAYCONFIG_TOPOLOGY_EXTEND: return Projection::Extend;
    case DISPLAYCONFIG_TOPOLOGY_EXTERNAL: return Projection::SecondScreenOnly;
    default: return Projection::Unknown;
    }
}

UINT32 TopologyFlag(Projection projection)
{
    switch (projection)
    {
    case Projection::PcScreenOnly: return SDC_TOPOLOGY_INTERNAL;
    case Projection::Duplicate: return SDC_TOPOLOGY_CLONE;
    case Projection::Extend: return SDC_TOPOLOGY_EXTEND;
    case Projection::SecondScreenOnly: return SDC_TOPOLOGY_EXTERNAL;
    default: return 0;
    }
}

int RefreshHz(UINT32 numerator, UINT32 denominator)
{
    return denominator ? static_cast<int>(std::lround(static_cast<double>(numerator) / denominator)) : 0;
}

void DisplayService::Start(std::function<void(const DisplaysState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);
    Refresh();
    Log::Info(L"displays: {} connected, {} showing, projection {}", m_state.connected, m_state.active.size(),
              static_cast<int>(m_state.projection));
}

void DisplayService::Refresh()
{
    DisplaysState state;
    state.switchingTo = m_state.switchingTo;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;

    // The projection mode, as Win+P shows it.
    DISPLAYCONFIG_TOPOLOGY_ID topology{};
    if (Query(QDC_DATABASE_CURRENT, paths, modes, &topology))
    {
        state.projection = ProjectionFromTopology(topology);
    }

    // The displays showing something: name, size and refresh rate.
    if (Query(QDC_ONLY_ACTIVE_PATHS, paths, modes))
    {
        for (const DISPLAYCONFIG_PATH_INFO& path : paths)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id };
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id };
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
            {
                continue;
            }
            DisplayInfo display;
            display.id = std::wstring(source.viewGdiDeviceName) + L"#" + std::to_wstring(path.targetInfo.id);
            display.internal = IsBuiltIn(target.outputTechnology);
            display.name = display.internal ? L"Built-in display"
                         : target.monitorFriendlyDeviceName[0] ? target.monitorFriendlyDeviceName : L"Display";
            display.refreshHz = RefreshHz(path.targetInfo.refreshRate.Numerator, path.targetInfo.refreshRate.Denominator);
            const UINT32 modeIndex = path.sourceInfo.modeInfoIdx;
            if (modeIndex < modes.size() && modes[modeIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            {
                display.width = static_cast<int>(modes[modeIndex].sourceMode.width);
                display.height = static_cast<int>(modes[modeIndex].sourceMode.height);
            }
            state.active.push_back(std::move(display));
        }
    }

    // Everything plugged in, including displays switched off by the projection mode.
    if (Query(QDC_ALL_PATHS, paths, modes))
    {
        std::set<std::pair<uint64_t, UINT32>> targets;
        for (const DISPLAYCONFIG_PATH_INFO& path : paths)
        {
            if (path.targetInfo.targetAvailable)
            {
                const uint64_t adapter = (static_cast<uint64_t>(static_cast<uint32_t>(path.targetInfo.adapterId.HighPart)) << 32) |
                                         path.targetInfo.adapterId.LowPart;
                targets.insert({ adapter, path.targetInfo.id });
            }
        }
        state.connected = static_cast<int>(targets.size());
    }

    if (state.projection == state.switchingTo)
    {
        state.switchingTo = Projection::Unknown;  // the change we asked for happened
    }
    m_state = std::move(state);
    Publish();
}

void DisplayService::SetProjection(Projection projection)
{
    if (projection == Projection::Unknown || projection == m_state.projection || m_state.switchingTo != Projection::Unknown)
    {
        return;
    }
    m_state.switchingTo = projection;
    Publish();
    SwitchAsync(projection);
}

winrt::fire_and_forget DisplayService::SwitchAsync(Projection projection)
{
    const std::weak_ptr<bool> alive = m_alive;
    const auto queue = m_queue;
    co_await winrt::resume_background();
    // The same request as Win+P. Windows rearranges the displays (they may
    // go dark for a moment) and then sends WM_DISPLAYCHANGE.
    const LONG result = SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | TopologyFlag(projection));
    if (!co_await winrt::resume_foreground(queue) || alive.expired())
    {
        co_return;
    }
    if (result != ERROR_SUCCESS)
    {
        Log::Info(L"displays: Windows refused that projection mode ({})", result);
    }
    m_state.switchingTo = Projection::Unknown;
    Refresh();
}

void DisplayService::Publish()
{
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
