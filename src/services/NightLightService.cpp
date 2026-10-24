#include "services/NightLightService.h"

#include <algorithm>
#include <ctime>
#include <string>

#include "platform/Log.h"

namespace
{
    // Where Windows keeps its settings records ("CloudStore"). Night light's
    // state has an older copy and, on newer Windows, a per-device copy; both
    // are kept in step.
    constexpr wchar_t kStore[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current";
    constexpr wchar_t kState[] = L"windows.data.bluelightreduction.bluelightreductionstate";
    constexpr wchar_t kStatePerDevice[] = L"windows.data.bluelightreduction.bluelightreductionstateperdevice";

    constexpr uint8_t kHeader[] = { 0x43, 0x42, 0x01, 0x00, 0x0A, 0x02, 0x01, 0x00, 0x2A, 0x06 };

    struct Layout
    {
        size_t stampEnd;  // just after the time stamp
        size_t sizeAt;    // the size byte
        size_t flagAt;    // where "10 00" is while night light is on
    };

    std::optional<Layout> Parse(const std::vector<uint8_t>& r)
    {
        if (r.size() < 24 || !std::equal(std::begin(kHeader), std::end(kHeader), r.begin()))
        {
            return std::nullopt;
        }
        // The time stamp is a variable-length number: 7 bits per byte, the
        // top bit set on every byte but the last.
        size_t i = sizeof(kHeader);
        while (i < r.size() && (r[i] & 0x80))
        {
            ++i;
        }
        const size_t stampEnd = i + 1;
        const size_t sizeAt = stampEnd + 3;
        if (sizeAt + 7 > r.size() || r[stampEnd] != 0x2A || r[stampEnd + 1] != 0x2B || r[stampEnd + 2] != 0x0E ||
            r[sizeAt + 1] != 0x43 || r[sizeAt + 2] != 0x42 || r[sizeAt + 3] != 0x01 || r[sizeAt + 4] != 0x00 ||
            r[sizeAt] >= 0x7E || sizeAt + 1 + r[sizeAt] > r.size())
        {
            return std::nullopt;
        }
        return Layout{ stampEnd, sizeAt, sizeAt + 5 };
    }

    bool FlagAt(const std::vector<uint8_t>& r, size_t at)
    {
        return r[at] == 0x10 && r[at + 1] == 0x00;
    }

    std::vector<uint8_t> ReadRecord(HKEY key)
    {
        DWORD size = 0;
        if (RegQueryValueExW(key, L"Data", nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0 || size > 4096)
        {
            return {};
        }
        std::vector<uint8_t> data(size);
        if (RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size) != ERROR_SUCCESS)
        {
            return {};
        }
        data.resize(size);
        return data;
    }
}

std::optional<bool> NightLightIsOn(const std::vector<uint8_t>& record)
{
    const auto layout = Parse(record);
    if (!layout)
    {
        return std::nullopt;
    }
    return FlagAt(record, layout->flagAt);
}

std::optional<std::vector<uint8_t>> NightLightWithState(const std::vector<uint8_t>& record, bool on, uint64_t unixSeconds)
{
    const auto layout = Parse(record);
    if (!layout)
    {
        return std::nullopt;
    }
    const bool isOn = FlagAt(record, layout->flagAt);

    // Same record with a fresh time stamp (so Windows sees it as the newest),
    // and "10 00" added or removed.
    std::vector<uint8_t> out(std::begin(kHeader), std::end(kHeader));
    do
    {
        const uint8_t low = static_cast<uint8_t>(unixSeconds & 0x7F);
        unixSeconds >>= 7;
        out.push_back(unixSeconds ? (low | 0x80) : low);
    } while (unixSeconds);
    const size_t sizeAt = out.size() + 3;
    out.insert(out.end(), record.begin() + layout->stampEnd, record.begin() + layout->flagAt);
    size_t rest = layout->flagAt;
    if (on && !isOn)
    {
        out.push_back(0x10);
        out.push_back(0x00);
        out[sizeAt] = static_cast<uint8_t>(out[sizeAt] + 2);
    }
    else if (!on && isOn)
    {
        rest += 2;
        out[sizeAt] = static_cast<uint8_t>(out[sizeAt] - 2);
    }
    out.insert(out.end(), record.begin() + rest, record.end());
    return out;
}

NightLightService::~NightLightService()
{
    m_alive.reset();
    for (auto& record : m_records)
    {
        if (record->wait)
        {
            SetThreadpoolWait(record->wait, nullptr, nullptr);
            WaitForThreadpoolWaitCallbacks(record->wait, TRUE);
            CloseThreadpoolWait(record->wait);
        }
        if (record->changed)
        {
            CloseHandle(record->changed);
        }
        RegCloseKey(record->key);
    }
}

void NightLightService::Start(std::function<void(const NightLightState&)> onChange)
{
    m_onChange = std::move(onChange);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
    m_alive = std::make_shared<bool>(true);

    // The record's keys: "<prefix>$<name>\<name>" for both names.
    HKEY store{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStore, 0, KEY_READ, &store) != ERROR_SUCCESS)
    {
        Log::Info(L"night light: Windows' settings store not found");
        return;
    }
    wchar_t name[512];
    for (DWORD index = 0;; ++index)
    {
        DWORD length = ARRAYSIZE(name);
        if (RegEnumKeyExW(store, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        {
            break;
        }
        const std::wstring folder(name, length);
        for (const wchar_t* suffix : { kState, kStatePerDevice })
        {
            if (folder.ends_with(std::wstring(L"$") + suffix))
            {
                auto record = std::make_unique<Record>();
                const std::wstring path = folder + L"\\" + suffix;
                if (RegOpenKeyExW(store, path.c_str(), 0, KEY_READ | KEY_SET_VALUE | KEY_NOTIFY, &record->key) == ERROR_SUCCESS)
                {
                    // Per-device copies last: Read() looks at them first.
                    m_records.insert(suffix == kStatePerDevice ? m_records.end() : m_records.begin(), std::move(record));
                }
            }
        }
    }
    RegCloseKey(store);

    for (auto& record : m_records)
    {
        record->changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        record->wait = CreateThreadpoolWait(OnChanged, this, nullptr);
        Arm(*record);
    }
    Read();
    Log::Info(L"night light: {}", !m_state.available ? L"not found (the button opens Settings)" : m_state.on ? L"on" : L"off");
}

void NightLightService::Arm(Record& record)
{
    // One notification per call: re-armed every time it fires.
    if (record.changed && record.wait &&
        RegNotifyChangeKeyValue(record.key, FALSE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, record.changed, TRUE) == ERROR_SUCCESS)
    {
        SetThreadpoolWait(record.wait, record.changed, nullptr);
    }
}

void CALLBACK NightLightService::OnChanged(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT wait, TP_WAIT_RESULT)
{
    // A thread-pool thread: continue on the UI thread.
    auto* self = static_cast<NightLightService*>(context);
    const std::weak_ptr<bool> alive = self->m_alive;
    self->m_queue.TryEnqueue([self, alive, wait]
    {
        if (alive.expired())
        {
            return;
        }
        for (auto& record : self->m_records)
        {
            if (record->wait == wait)
            {
                self->Arm(*record);
            }
        }
        self->Read();
    });
}

void NightLightService::Read()
{
    // The per-device copy is the newer one; either says the same when both exist.
    NightLightState state;
    for (auto it = m_records.rbegin(); it != m_records.rend() && !state.available; ++it)
    {
        if (const auto on = NightLightIsOn(ReadRecord((*it)->key)))
        {
            state.available = true;
            state.on = *on;
        }
    }
    const bool changed = state.available != m_state.available || state.on != m_state.on;
    m_state = state;
    if (changed && m_onChange)
    {
        m_onChange(m_state);
    }
}

bool NightLightService::Set(bool on)
{
    bool wrote = false;
    for (auto& record : m_records)
    {
        if (const auto updated = NightLightWithState(ReadRecord(record->key), on, static_cast<uint64_t>(std::time(nullptr))))
        {
            wrote |= RegSetValueExW(record->key, L"Data", 0, REG_BINARY, updated->data(), static_cast<DWORD>(updated->size())) == ERROR_SUCCESS;
        }
    }
    if (!wrote)
    {
        Log::Info(L"night light: couldn't switch it");
        return false;
    }
    // Windows notices the change and switches; our own notification confirms it.
    m_state.on = on;
    if (m_onChange)
    {
        m_onChange(m_state);
    }
    return true;
}
