#pragma once

#include <memory>

#include <winrt/Windows.System.h>

#include "views/View.h"

// A panel with one of every control, wired to nothing, for trying out and
// tuning the motion (PLAN.md Phase 3). Opened from the tray menu.
class ControlKitDemo : public View
{
public:
    using View::View;
    void Build() override;

private:
    void UpdateVolumeIcon();

    std::unique_ptr<RoundedRect> m_art;
    std::unique_ptr<Label> m_artIcon, m_title, m_subtitle, m_volumeCaption, m_brightnessCaption;
    std::unique_ptr<IconButton> m_previous, m_playPause, m_next, m_volumeButton, m_brightnessIcon;
    std::unique_ptr<Slider> m_volume, m_brightness;
    std::unique_ptr<Segmented> m_mode;
    std::unique_ptr<ToggleTile> m_wifi, m_bluetooth;
    winrt::Windows::System::DispatcherQueueTimer m_wifiTimer{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_bluetoothTimer{ nullptr };
    bool m_playing = false;
    bool m_muted = false;
};
