#pragma once

#include <memory>

#include "services/AudioService.h"
#include "services/BluetoothService.h"
#include "services/BrightnessService.h"
#include "services/MediaState.h"
#include "services/PowerService.h"
#include "services/RadioService.h"
#include "services/WifiService.h"
#include "views/Actions.h"
#include "views/View.h"
#include "views/sections/ConnectionsSection.h"
#include "views/sections/DisplaysSection.h"
#include "views/sections/NowPlayingSection.h"
#include "views/sections/SoundSection.h"
#include "views/sections/StatusSection.h"

// The full panel a click opens: a wide, horizontal layout.
//
//   10:42  Saturday, 24 October                       [battery] 84 %  2 h left
//   -----------------------------------------------------------------------
//   Now playing     |  Sound            |  Displays     |  Connections
//   artwork, song   |  main volume      |  brightness   |  Wi-Fi / Bluetooth
//   controls        |  apps (mixer)     |  per display  |  networks / devices
class ControlCenter : public View
{
public:
    ControlCenter(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)),
          m_status(ui, actions),
          m_nowPlaying(ui, actions),
          m_sound(ui, actions),
          m_displays(ui, actions),
          m_connections(ui, actions)
    {
    }

    void Build() override;
    void SetMedia(const MediaState& media) { m_nowPlaying.SetMedia(media, Visible()); }
    void SetAudio(const AudioState& audio) { m_sound.SetAudio(audio, Visible()); }
    void SetRadios(const RadiosState& radios) { m_connections.SetRadios(radios, Visible()); }
    void SetWifi(const WifiState& wifi) { m_connections.SetWifi(wifi, Visible()); }
    void SetBluetooth(const BluetoothState& bluetooth) { m_connections.SetBluetooth(bluetooth, Visible()); }
    void SetPower(const PowerState& power) { m_status.SetPower(power); }

    // Returns true if displays came or went: then the panel needs Build()
    // again, because its height changes.
    bool SetBrightness(const BrightnessState& brightness) { return m_displays.SetBrightness(brightness, Visible()); }

protected:
    void OnVisibilityChanged(bool visible) override;

private:
    // Hidden content never animates (see View::Retire): every update passes
    // Visible() as "animate".
    StatusSection m_status;
    NowPlayingSection m_nowPlaying;
    SoundSection m_sound;
    DisplaysSection m_displays;
    ConnectionsSection m_connections;
    std::unique_ptr<RoundedRect> m_divider;
};
