#include "platform/Settings.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform/Log.h"

namespace json = winrt::Windows::Data::Json;

namespace
{
    // JSON has no comments, but they're handy for explaining settings, so we
    // blank out "//" comments (outside of strings) before parsing.
    std::wstring StripComments(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());
        bool inString = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wchar_t c = text[i];
            if (inString)
            {
                result += c;
                if (c == L'\\' && i + 1 < text.size())
                {
                    result += text[++i];
                }
                else if (c == L'"')
                {
                    inString = false;
                }
            }
            else if (c == L'/' && i + 1 < text.size() && text[i + 1] == L'/')
            {
                while (i < text.size() && text[i] != L'\n')
                {
                    ++i;
                }
                result += L'\n';
            }
            else
            {
                inString = (c == L'"');
                result += c;
            }
        }
        return result;
    }

    // Reads typed values out of a JSON object. A value of the wrong type keeps
    // its default and is reported, instead of failing the whole file.
    class Reader
    {
    public:
        Reader(json::JsonObject object, std::wstring path, std::wstring& message)
            : m_object(std::move(object)), m_path(std::move(path)), m_message(message) {}

        Reader Child(const wchar_t* key) const
        {
            json::JsonObject child{ nullptr };
            if (auto value = Find(key, json::JsonValueType::Object))
            {
                child = value.GetObject();
            }
            return Reader(child, m_path + key + L".", m_message);
        }

        void Number(const wchar_t* key, int& target, int low, int high) const
        {
            double value = target;
            Number(key, value, low, high);
            target = static_cast<int>(std::lround(value));
        }

        void Number(const wchar_t* key, float& target, float low, float high) const
        {
            double value = target;
            Number(key, value, low, high);
            target = static_cast<float>(value);
        }

        void Number(const wchar_t* key, double& target, double low, double high) const
        {
            if (auto value = Find(key, json::JsonValueType::Number))
            {
                target = std::clamp(value.GetNumber(), low, high);
            }
        }

        void Bool(const wchar_t* key, bool& target) const
        {
            if (auto value = Find(key, json::JsonValueType::Boolean))
            {
                target = value.GetBoolean();
            }
        }

        void String(const wchar_t* key, std::wstring& target) const
        {
            if (auto value = Find(key, json::JsonValueType::String))
            {
                target = value.GetString();
            }
        }

        void SpringValue(const wchar_t* key, Spring& target) const
        {
            const Reader spring = Child(key);
            spring.Number(L"dampingRatio", target.dampingRatio, 0.1f, 2.0f);
            spring.Number(L"periodMs", target.periodMs, 5.0f, 1000.0f);
        }

        void LowerCaseStrings(const wchar_t* key, std::vector<std::wstring>& target) const
        {
            if (auto value = Find(key, json::JsonValueType::Array))
            {
                target.clear();
                for (const auto& item : value.GetArray())
                {
                    if (item.ValueType() == json::JsonValueType::String)
                    {
                        std::wstring text{ item.GetString() };
                        std::transform(text.begin(), text.end(), text.begin(), ::towlower);
                        target.push_back(std::move(text));
                    }
                }
            }
        }

    private:
        json::IJsonValue Find(const wchar_t* key, json::JsonValueType type) const
        {
            if (!m_object || !m_object.HasKey(key))
            {
                return nullptr;
            }
            auto value = m_object.GetNamedValue(key);
            if (value.ValueType() != type)
            {
                m_message += L"\"" + m_path + key + L"\" has the wrong type and was ignored. ";
                return nullptr;
            }
            return value;
        }

        json::JsonObject m_object;
        std::wstring m_path;
        std::wstring& m_message;
    };

    std::wstring ExeFolder()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path().wstring();
    }

    std::wstring FindSettingsFile()
    {
        std::vector<std::filesystem::path> candidates{ std::filesystem::path(ExeFolder()) / L"settings.json" };
#ifdef ISLAND_CONFIG_DIR
        // Builds made from the repo read the repo's copy, so you can edit
        // config/settings.json in VS Code and see the change immediately.
        candidates.push_back(std::filesystem::path(ISLAND_CONFIG_DIR) / L"settings.json");
#endif
        for (const auto& candidate : candidates)
        {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error))
            {
                return candidate.lexically_normal().wstring();
            }
        }
        return {};
    }

    bool ReadUtf8File(const std::wstring& path, std::wstring& text)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string bytes = buffer.str();
        if (bytes.starts_with("\xEF\xBB\xBF"))  // UTF-8 byte order mark
        {
            bytes.erase(0, 3);
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        text.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
        return true;
    }

    FILETIME LastWriteTime(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
        return data.ftLastWriteTime;
    }
}

bool ParseSettings(std::wstring_view text, Settings& out, std::wstring& message)
{
    out = Settings{};
    message.clear();

    json::JsonObject root{ nullptr };
    if (!json::JsonObject::TryParse(StripComments(text), root))
    {
        message = L"not valid JSON";
        return false;
    }

    try
    {
        const Reader settings(root, L"", message);

        const Reader games = settings.Child(L"games");
        games.LowerCaseStrings(L"blocklist", out.games.blocklist);
        games.Number(L"showDelayMs", out.games.showDelayMs, 0, 5000);

        LayoutSettings& l = out.layout;
        const Reader layout = settings.Child(L"layout");
        layout.Number(L"idleWidth", l.idleWidth, 60.0f, 600.0f);
        layout.Number(L"idleHeight", l.idleHeight, 12.0f, 80.0f);
        layout.Number(L"topGap", l.topGap, 0.0f, 40.0f);
        layout.Number(L"slimWidth", l.slimWidth, 20.0f, 600.0f);
        layout.Number(L"slimHeight", l.slimHeight, 2.0f, 20.0f);
        layout.Number(L"peekWidth", l.peekWidth, 200.0f, 800.0f);
        layout.Number(L"peekHeight", l.peekHeight, 36.0f, 120.0f);
        layout.Number(L"panelWidth", l.panelWidth, 600.0f, 2400.0f);
        layout.Number(L"panelMargin", l.panelMargin, 0.0f, 600.0f);
        layout.Number(L"panelRadius", l.panelRadius, 0.0f, 60.0f);

        GlassSettings& g = out.glass;
        const Reader glass = settings.Child(L"glass");
        glass.Number(L"tintOpacity", g.tintOpacity, 0.0f, 1.0f);
        glass.Number(L"saturation", g.saturation, 0.0f, 3.0f);
        glass.Number(L"brightness", g.brightness, 0.2f, 2.0f);
        glass.Number(L"noiseOpacity", g.noiseOpacity, 0.0f, 0.3f);
        glass.Number(L"rimOpacity", g.rimOpacity, 0.0f, 3.0f);
        glass.String(L"accent", g.accent);

        MotionSettings& m = out.motion;
        const Reader motion = settings.Child(L"motion");
        motion.SpringValue(L"morph", m.morph);
        motion.SpringValue(L"press", m.press);
        motion.SpringValue(L"toggle", m.toggle);
        motion.SpringValue(L"slider", m.slider);
        motion.Number(L"hoverFadeMs", m.hoverFadeMs, 0, 2000);
        motion.Number(L"contentFadeOutMs", m.contentFadeOutMs, 0, 2000);
        motion.Number(L"contentFadeInMs", m.contentFadeInMs, 0, 2000);
        motion.Number(L"contentFadeInDelayMs", m.contentFadeInDelayMs, 0, 2000);
        motion.Number(L"hoverIntentMs", m.hoverIntentMs, 0, 2000);
        motion.Number(L"leaveDelayMs", m.leaveDelayMs, 0, 5000);
        motion.Number(L"expandedLeaveDelayMs", m.expandedLeaveDelayMs, 0, 5000);
    }
    catch (const winrt::hresult_error& error)
    {
        out = Settings{};
        message = error.message().c_str();
        return false;
    }
    return true;
}

SettingsFile::~SettingsFile()
{
    if (m_wait)
    {
        // INVALID_HANDLE_VALUE: wait for a callback that's already running to finish.
        UnregisterWaitEx(m_wait, INVALID_HANDLE_VALUE);
    }
    if (m_folderChange != INVALID_HANDLE_VALUE)
    {
        FindCloseChangeNotification(m_folderChange);
    }
}

void SettingsFile::Load()
{
    m_path = FindSettingsFile();
    if (m_path.empty())
    {
        Log::Info(L"settings: no settings.json found, using defaults");
        m_settings = Settings{};
        return;
    }
    Settings loaded;
    Read(loaded);  // on failure `loaded` already holds the defaults
    m_settings = std::move(loaded);
}

bool SettingsFile::Read(Settings& out)
{
    m_lastWrite = LastWriteTime(m_path);
    std::wstring text;
    if (!ReadUtf8File(m_path, text))
    {
        Log::Info(L"settings: couldn't read {}", m_path);
        out = Settings{};
        return false;
    }
    std::wstring message;
    const bool ok = ParseSettings(text, out, message);
    Log::Info(L"settings: {} {}{}", ok ? L"loaded" : L"ERROR in", m_path, message.empty() ? L"" : L" - " + message);
    return ok;
}

void SettingsFile::Watch(std::function<void()> onChanged)
{
    if (m_path.empty())
    {
        return;
    }
    m_onChanged = std::move(onChanged);
    m_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

    // Editors often save in several steps (write, rename, touch), so we wait
    // until the folder has been quiet for a moment before reading.
    m_debounce = m_queue.CreateTimer();
    m_debounce.Interval(std::chrono::milliseconds(150));
    m_debounce.IsRepeating(false);
    m_debounce.Tick([this](auto&&, auto&&) { Reload(); });

    // Windows signals this handle whenever something in the folder changes; a
    // thread-pool thread waits on it, so nothing runs until a file is saved.
    const std::wstring folder = std::filesystem::path(m_path).parent_path().wstring();
    m_folderChange = FindFirstChangeNotificationW(folder.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
    if (m_folderChange == INVALID_HANDLE_VALUE ||
        !RegisterWaitForSingleObject(&m_wait, m_folderChange, OnFolderChanged, this, INFINITE, WT_EXECUTEDEFAULT))
    {
        Log::Info(L"settings: can't watch {} for changes", folder);
    }
}

void CALLBACK SettingsFile::OnFolderChanged(void* context, BOOLEAN)
{
    // Runs on a thread-pool thread: re-arm the handle, then hop to the UI thread.
    auto* self = static_cast<SettingsFile*>(context);
    FindNextChangeNotification(self->m_folderChange);
    self->m_queue.TryEnqueue([self]
    {
        self->m_debounce.Stop();
        self->m_debounce.Start();
    });
}

void SettingsFile::Reload()
{
    const FILETIME written = LastWriteTime(m_path);
    if (CompareFileTime(&written, &m_lastWrite) == 0)
    {
        return;  // something else in the folder changed
    }
    Settings loaded;
    if (Read(loaded))
    {
        m_settings = std::move(loaded);
        if (m_onChanged)
        {
            m_onChanged();
        }
    }
}
