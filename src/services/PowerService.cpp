#include "services/PowerService.h"

#include <winrt/Windows.Devices.Power.h>
#include <winrt/Windows.System.Power.h>

#include <algorithm>
#include <chrono>
#include <format>

#include "platform/Log.h"
#include "render/Glyphs.h"

namespace power = winrt::Windows::System::Power;
using winrt::Windows::Devices::Power::Battery;

namespace
{
    std::wstring Duration(int minutes)
    {
        if (minutes < 60)
        {
            return std::format(L"{} min", minutes);
        }
        return minutes % 60 == 0 ? std::format(L"{} h", minutes / 60) : std::format(L"{} h {} min", minutes / 60, minutes % 60);
    }
}

std::wstring PowerDescription(const PowerState& power)
{
    if (!power.hasBattery)
    {
        return L"Plugged in";
    }
    std::wstring text;
    if (power.charging)
    {
        text = power.minutesToFull > 0 ? L"Charging · " + Duration(power.minutesToFull) + L" to full" : L"Charging";
    }
    else if (power.pluggedIn)
    {
        text = power.percent >= 99 ? L"Fully charged" : L"Plugged in";
    }
    else
    {
        text = power.minutesLeft > 0 ? Duration(power.minutesLeft) + L" left" : L"On battery";
    }
    if (power.energySaver)
    {
        text += L" · Energy saver";
    }
    return text;
}

wchar_t BatteryGlyph(const PowerState& power)
{
    if (!power.hasBattery)
    {
        return Glyph::Plug;
    }
    const int level = std::clamp((power.percent + 5) / 10, 0, 10);
    return static_cast<wchar_t>((power.charging ? Glyph::BatteryCharging0 : Glyph::Battery0) + level);
}

PowerService::~PowerService()
{
    m_alive.reset();
    try
    {
        power::PowerManager::RemainingChargePercentChanged(m_percentToken);
        power::PowerManager::BatteryStatusChanged(m_batteryToken);
        power::PowerManager::PowerSupplyStatusChanged(m_supplyToken);
        power::PowerManager::RemainingDischargeTimeChanged(m_timeToken);
        power::PowerManager::EnergySaverStatusChanged(m_saverToken);
        Battery::AggregateBattery().ReportUpdated(m_reportToken);
    }
    catch (const winrt::hresult_error&)
    {
    }
}

void PowerService::Start(std::function<void(const PowerState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);

    // All of these are raised on background threads; re-read on the UI thread.
    const auto queue = m_queue;
    const std::weak_ptr<bool> alive = m_alive;
    auto changed = [queue, alive, this](auto&&...)
    {
        queue.TryEnqueue([alive, this]
        {
            if (!alive.expired())
            {
                Read();
            }
        });
    };
    try
    {
        m_percentToken = power::PowerManager::RemainingChargePercentChanged(changed);
        m_batteryToken = power::PowerManager::BatteryStatusChanged(changed);
        m_supplyToken = power::PowerManager::PowerSupplyStatusChanged(changed);
        m_timeToken = power::PowerManager::RemainingDischargeTimeChanged(changed);
        m_saverToken = power::PowerManager::EnergySaverStatusChanged(changed);
        m_reportToken = Battery::AggregateBattery().ReportUpdated(changed);
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"power: can't follow battery changes ({})", error.message());
    }
    Read();
    if (m_state.hasBattery)
    {
        Log::Info(L"power: battery at {} %, {}", m_state.percent, PowerDescription(m_state));
    }
    else
    {
        Log::Info(L"power: no battery");
    }
}

void PowerService::Read()
{
    PowerState state;
    try
    {
        const auto battery = power::PowerManager::BatteryStatus();
        state.hasBattery = battery != power::BatteryStatus::NotPresent;
        state.percent = power::PowerManager::RemainingChargePercent();
        state.charging = battery == power::BatteryStatus::Charging;
        state.pluggedIn = power::PowerManager::PowerSupplyStatus() != power::PowerSupplyStatus::NotPresent;
        state.energySaver = power::PowerManager::EnergySaverStatus() == power::EnergySaverStatus::On;

        // Windows reports a huge number when it can't estimate (or is on mains).
        const auto left = std::chrono::duration_cast<std::chrono::minutes>(power::PowerManager::RemainingDischargeTime()).count();
        if (!state.pluggedIn && left > 0 && left < 48 * 60)
        {
            state.minutesLeft = static_cast<int>(left);
        }

        // Time to full isn't reported directly: work it out from the charge rate.
        if (state.charging)
        {
            const auto report = Battery::AggregateBattery().GetReport();
            const auto rate = report.ChargeRateInMilliwatts();
            const auto full = report.FullChargeCapacityInMilliwattHours();
            const auto now = report.RemainingCapacityInMilliwattHours();
            if (rate && full && now && rate.Value() > 0 && full.Value() > now.Value())
            {
                state.minutesToFull = static_cast<int>(60.0 * (full.Value() - now.Value()) / rate.Value());
            }
        }
    }
    catch (const winrt::hresult_error& error)
    {
        Log::Info(L"power: couldn't read the battery ({})", error.message());
    }
    m_state = state;
    if (m_onChange)
    {
        m_onChange(m_state);
    }
}
