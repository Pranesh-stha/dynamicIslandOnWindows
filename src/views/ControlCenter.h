#pragma once

#include <memory>
#include <vector>

#include "services/AudioService.h"
#include "services/BrightnessService.h"
#include "services/MediaState.h"
#include "services/RadioService.h"
#include "views/Actions.h"
#include "views/View.h"

// The full panel a click opens: now playing at the top, then volume, one
// brightness slider per display, and the Wi-Fi and Bluetooth toggles.
class ControlCenter : public View
{
public:
    ControlCenter(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent, const IslandActions& actions)
        : View(ui, std::move(parent)), m_actions(actions) {}

    void Build() override;
    void SetMedia(const MediaState& media);
    void SetAudio(const AudioState& audio);
    void SetRadios(const RadiosState& radios);

    // Returns true if the list of displays changed: then the panel needs
    // Build() again, because its height changes.
    bool SetBrightness(const BrightnessState& brightness);

protected:
    void OnVisibilityChanged(bool visible) override;

private:
    struct BrightnessRow
    {
        std::wstring id;
        std::unique_ptr<Label> caption;
        std::unique_ptr<IconButton> icon;
        std::unique_ptr<Slider> slider;
    };

    float BuildMedia(float top);
    float BuildSlider(float top, std::unique_ptr<Label>& caption, std::unique_ptr<IconButton>& icon, wchar_t glyph,
                      std::unique_ptr<Slider>& slider);
    float BuildToggles(float top);
    void ApplyMedia(bool animate);
    void ApplyAudio(bool animate);
    void ApplyBrightness(bool animate);
    void ApplyRadios(bool animate);
    void UpdateProgress();
    void AddControl(Control& control);

    const IslandActions& m_actions;
    MediaState m_media;
    AudioState m_audio;
    BrightnessState m_brightness;
    RadiosState m_radios;
    float m_pad = 0.0f;
    float m_textX = 0.0f, m_textWidth = 0.0f, m_artTop = 0.0f;

    std::unique_ptr<Artwork> m_art;
    std::unique_ptr<Label> m_title, m_artist;
    std::unique_ptr<TextButton> m_app;
    std::unique_ptr<IconButton> m_previous, m_playPause, m_next;
    std::unique_ptr<ProgressBar> m_progress;

    std::unique_ptr<Label> m_volumeCaption;
    std::unique_ptr<IconButton> m_volumeIcon;
    std::unique_ptr<Slider> m_volume;
    std::vector<BrightnessRow> m_brightnessRows;
    std::unique_ptr<ToggleTile> m_wifi, m_bluetooth;
};
