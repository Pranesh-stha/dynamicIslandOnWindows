#include "services/PowerService.h"

#include <powrprof.h>

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
    constexpr unsigned long kAlways = 100;  // "start at 100 % battery" = always on
    constexpr wchar_t kIslandKey[] = L"Software\\Island\\EnergySaver";  // where Island remembers your own levels

    std::optional<EnergySaverLevels> ReadPlanLevels()
    {
        GUID* scheme = nullptr;
        if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        EnergySaverLevels levels;
        const bool ok = PowerReadACValueIndex(nullptr, scheme, &GUID_ENERGY_SAVER_SUBGROUP, &GUID_ENERGY_SAVER_BATTERY_THRESHOLD, &levels.pluggedIn) == ERROR_SUCCESS &&
                        PowerReadDCValueIndex(nullptr, scheme, &GUID_ENERGY_SAVER_SUBGROUP, &GUID_ENERGY_SAVER_BATTERY_THRESHOLD, &levels.onBattery) == ERROR_SUCCESS;
        LocalFree(scheme);
        return ok ? std::optional(levels) : std::nullopt;
    }

    bool WritePlanLevels(const EnergySaverLevels& levels)
    {
        GUID* scheme = nullptr;
        if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS)
        {
            return false;
        }
        // Written to the active power plan, then the plan is applied again so Windows picks it up.
        const bool ok = PowerWriteACValueIndex(nullptr, scheme, &GUID_ENERGY_SAVER_SUBGROUP, &GUID_ENERGY_SAVER_BATTERY_THRESHOLD, levels.pluggedIn) == ERROR_SUCCESS &&
                        PowerWriteDCValueIndex(nullptr, scheme, &GUID_ENERGY_SAVER_SUBGROUP, &GUID_ENERGY_SAVER_BATTERY_THRESHOLD, levels.onBattery) == ERROR_SUCCESS &&
                        PowerSetActiveScheme(nullptr, scheme) == ERROR_SUCCESS;
        LocalFree(scheme);
        return ok;
    }

    std::optional<EnergySaverLevels> ReadRemembered()
    {
        EnergySaverLevels levels;
        DWORD size = sizeof(DWORD);
        if (RegGetValueW(HKEY_CURRENT_USER, kIslandKey, L"PluggedIn", RRF_RT_REG_DWORD, nullptr, &levels.pluggedIn, &size) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        size = sizeof(DWORD);
        if (RegGetValueW(HKEY_CURRENT_USER, kIslandKey, L"OnBattery", RRF_RT_REG_DWORD, nullptr, &levels.onBattery, &size) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        return levels;
    }

    void Remember(const EnergySaverLevels& levels)
    {
        RegSetKeyValueW(HKEY_CURRENT_USER, kIslandKey, L"PluggedIn", REG_DWORD, &levels.pluggedIn, sizeof(DWORD));
        RegSetKeyValueW(HKEY_CURRENT_USER, kIslandKey, L"OnBattery", REG_DWORD, &levels.onBattery, sizeof(DWORD));
    }

    void Forget()
    {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kIslandKey, L"PluggedIn");
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kIslandKey, L"OnBattery");
    }

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

EnergySaverChange PlanEnergySaver(bool on, const EnergySaverLevels& current, const std::optional<EnergySaverLevels>& remembered)
{
    EnergySaverChange change;
    const EnergySaverLevels always{ kAlways, kAlways };
    if (on)
    {
        if (current == always)
        {
            change.openSettings = true;  // already "always" and still off: something else (a policy) decides
            return change;
        }
        change.write = true;
        change.levels = always;
        // Keep the levels from before the first switch, never the "always" ones.
        change.remember = !remembered.has_value();
        change.previous = current;
        return change;
    }
    if (remembered)
    {
        change.write = true;
        change.levels = *remembered;
        change.forget = true;
        return change;
    }
    // On because you chose "always", or because the battery is low: that's
    // your setting, so Settings is where to change it.
    change.openSettings = true;
    return change;
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
    if (m_saverNotify)
    {
        PowerSettingUnregisterNotification(m_saverNotify);
    }
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
    // Windows reports the current energy saver status right away, then every change.
    m_saverParams = { OnSaverStatus, this };
    if (PowerSettingRegisterNotification(&GUID_ENERGY_SAVER_STATUS, DEVICE_NOTIFY_CALLBACK, &m_saverParams, &m_saverNotify) != ERROR_SUCCESS)
    {
        m_saverNotify = nullptr;
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
        if (m_saverStatus >= 0)
        {
            state.energySaver = m_saverStatus != ENERGY_SAVER_OFF;
            state.energySaverAvailable = true;
        }
        else
        {
            const auto saver = power::PowerManager::EnergySaverStatus();
            state.energySaver = saver == power::EnergySaverStatus::On;
            state.energySaverAvailable = saver != power::EnergySaverStatus::Disabled;
        }

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

ULONG CALLBACK PowerService::OnSaverStatus(PVOID context, ULONG type, PVOID data)
{
    // A system thread: continue on the UI thread.
    auto* self = static_cast<PowerService*>(context);
    const auto* setting = static_cast<const POWERBROADCAST_SETTING*>(data);
    if (type == PBT_POWERSETTINGCHANGE && setting && setting->DataLength >= sizeof(DWORD))
    {
        const DWORD status = *reinterpret_cast<const DWORD*>(setting->Data);
        const std::weak_ptr<bool> alive = self->m_alive;
        self->m_queue.TryEnqueue([self, alive, status]
        {
            if (!alive.expired())
            {
                self->m_saverStatus = static_cast<int>(status);
                self->Read();
            }
        });
    }
    return 0;
}

void PowerService::SetEnergySaver(bool on)
{
    const auto current = ReadPlanLevels();
    const EnergySaverChange change = current ? PlanEnergySaver(on, *current, ReadRemembered()) : EnergySaverChange{ .openSettings = true };
    if (change.openSettings || (change.write && !WritePlanLevels(change.levels)))
    {
        Log::Info(L"power: can't switch energy saver here; opening Settings");
        if (onCantSwitch)
        {
            onCantSwitch();
        }
        return;
    }
    // What to put back if Windows doesn't follow.
    m_undo = Undo{ *current, ReadRemembered() };
    if (change.remember)
    {
        Remember(change.previous);
    }
    if (change.forget)
    {
        Forget();
    }

    // Windows reports the switch through EnergySaverStatusChanged (-> Read).
    // If it hasn't after a few seconds, this way doesn't work here: undo it.
    if (!m_check)
    {
        m_check = m_queue.CreateTimer();
        m_check.IsRepeating(false);
        m_check.Interval(std::chrono::seconds(4));
    }
    const std::weak_ptr<bool> alive = m_alive;
    m_check.Tick([this, alive, on](auto&&, auto&&)
    {
        if (alive.expired() || m_state.energySaver == on || !m_undo)
        {
            return;
        }
        Log::Info(L"power: Windows didn't switch energy saver; putting the levels back");
        WritePlanLevels(m_undo->levels);
        if (m_undo->remembered)
        {
            Remember(*m_undo->remembered);
        }
        else
        {
            Forget();
        }
        m_undo.reset();
        if (onCantSwitch)
        {
            onCantSwitch();
        }
    });
    m_check.Stop();
    m_check.Start();
}
