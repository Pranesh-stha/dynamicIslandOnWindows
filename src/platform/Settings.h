#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <winrt/Windows.System.h>

#include "render/Motion.h"

// Sizes in DIPs (device-independent pixels: 1 DIP = 1 pixel at 100 % scaling,
// 1.25 pixels at 125 %, and so on).
struct LayoutSettings
{
    bool autoHide = true;       // out of sight until the mouse touches the top edge (like an auto-hide taskbar)
    float revealWidth = 300.0f; // ...in the middle this wide
    float idleWidth = 200.0f;   // the resting pill
    float idleHeight = 34.0f;
    float topGap = 6.0f;        // space between the top of the screen and the island
    float slimWidth = 150.0f;   // the line over maximized windows
    float slimHeight = 4.0f;
    float peekWidth = 340.0f;   // hover
    float peekHeight = 56.0f;
    float panelWidth = 1180.0f; // click: the control center (its height follows its content)
    float panelMargin = 80.0f;  // the control center keeps at least this far from the screen's sides
    float panelRadius = 30.0f;
};

struct GlassSettings
{
    // "clear": a light blur of the live screen behind, bent at the edges
    // like real glass (needs screen capture, see GlassCapture). "frosted":
    // Windows' own heavy blur.
    std::wstring style = L"clear";
    float blur = 6.0f;            // clear glass: how soft what's behind looks (DIP)
    float refraction = 1.0f;      // clear glass: how strongly the edges bend (0 = flat)
    int fps = 15;                 // clear glass, Peek and panel: new pictures of what's behind a second (each costs the GPU)
    float tintOpacity = 0.42f;    // dark tint over the blur: 0 = clear, 1 = solid
    float saturation = 1.6f;      // colour boost of what's behind (1 = unchanged)
    float brightness = 1.0f;      // brightness of what's behind (1 = unchanged)
    float noiseOpacity = 0.035f;  // faint grain that hides banding in the blur
    float rimOpacity = 1.0f;      // the thin light edge along the outline
    // Colour of "on" toggles and other highlights: "auto" (Windows' accent
    // colour, or blue if that is grey) or "#RRGGBB".
    std::wstring accent = L"auto";
};

struct GameSettings
{
    // Exe names (lower case) that hide the island whenever they're the app in
    // front, e.g. L"somegame.exe". An escape hatch for anything the automatic
    // fullscreen detection misses.
    std::vector<std::wstring> blocklist;

    // How long nothing may be fullscreen before the island comes back. Hiding
    // is always instant; only coming back waits, so alt-tab doesn't flicker.
    int showDelayMs = 500;
};

struct Settings
{
    GameSettings games;
    LayoutSettings layout;
    GlassSettings glass;
    MotionSettings motion;
};

// Reads JSON text into `out`. Keys that are missing keep their defaults, and
// "//" comments are allowed. Returns false on invalid JSON (then `out` holds
// the defaults). `message` explains errors and wrongly typed values.
bool ParseSettings(std::wstring_view text, Settings& out, std::wstring& message);

// settings.json on disk: found next to Island.exe, or (for builds made from
// the repo) in the repo's config folder. Edits are picked up while Island runs.
class SettingsFile
{
public:
    SettingsFile() = default;
    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;
    ~SettingsFile();

    // Reads the file. A missing or broken file means defaults.
    void Load();

    // Calls onChanged on this thread shortly after the file is saved. A save
    // with an error keeps the previous settings (and logs the error).
    void Watch(std::function<void()> onChanged);

    const Settings& Current() const { return m_settings; }
    const std::wstring& Path() const { return m_path; }

private:
    static void CALLBACK OnFolderChanged(void* context, BOOLEAN timedOut);
    bool Read(Settings& out);
    void Reload();

    Settings m_settings;
    std::wstring m_path;
    FILETIME m_lastWrite{};
    HANDLE m_folderChange = INVALID_HANDLE_VALUE;
    HANDLE m_wait = nullptr;
    winrt::Windows::System::DispatcherQueue m_queue{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_debounce{ nullptr };
    std::function<void()> m_onChanged;
};
