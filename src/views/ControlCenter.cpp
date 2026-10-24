#include "views/ControlCenter.h"

#include "platform/Settings.h"
#include "render/Glyphs.h"

namespace
{
    wchar_t VolumeGlyph(const AudioState& audio)
    {
        if (audio.muted || !audio.available)
        {
            return Glyph::Mute;
        }
        return audio.volume < 0.01f ? Glyph::Volume0
             : audio.volume < 0.34f ? Glyph::Volume1
             : audio.volume < 0.67f ? Glyph::Volume2
                                    : Glyph::Volume3;
    }

    bool SameDisplays(const BrightnessState& a, const BrightnessState& b)
    {
        if (a.displays.size() != b.displays.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.displays.size(); ++i)
        {
            if (a.displays[i].id != b.displays[i].id)
            {
                return false;
            }
        }
        return true;
    }
}

void ControlCenter::Build()
{
    const float width = m_ui.EvenPx(m_ui.layout->panelWidth);
    ResetLayer(width, 0.0f);
    m_pad = m_ui.Px(18.0f);

    float y = m_pad;
    y = BuildMedia(y);
    y += m_ui.Px(18.0f);

    // Volume. The speaker icon mutes; right-click opens the sound settings.
    y = BuildSlider(y, m_volumeCaption, m_volumeIcon, VolumeGlyph(m_audio), m_volume);
    m_volumeIcon->onClick = [this] { if (m_actions.toggleMute) m_actions.toggleMute(); };
    m_volume->onChange = [this](float value, bool)
    {
        if (m_actions.setVolume)
        {
            m_actions.setVolume(value);
        }
    };
    auto soundSettings = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:sound"); };
    m_volume->onRightClick = soundSettings;
    m_volumeIcon->onRightClick = soundSettings;

    // One brightness slider per display (the island's own monitor first).
    m_brightnessRows.clear();
    for (const DisplayBrightness& display : m_brightness.displays)
    {
        BrightnessRow row;
        row.id = display.id;
        y = BuildSlider(y, row.caption, row.icon, Glyph::Brightness, row.slider);
        row.icon->SetInteractive(false);
        const std::wstring id = display.id;
        row.slider->onChange = [this, id](float value, bool)
        {
            if (m_actions.setBrightness)
            {
                m_actions.setBrightness(id, value);
            }
        };
        row.slider->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:display"); };
        m_brightnessRows.push_back(std::move(row));
    }

    y += m_ui.Px(6.0f);
    y = BuildToggles(y);
    SetHeight(y + m_pad);

    ApplyAudio(false);
    ApplyBrightness(false);
    ApplyRadios(false);
}

void ControlCenter::AddControl(Control& control)
{
    m_layer.Children().InsertAtTop(control.Visual());
    m_controls.Add(control);
}

// --- Now playing -------------------------------------------------------------

float ControlCenter::BuildMedia(float top)
{
    const Ui& ui = m_ui;
    m_artTop = top;
    m_art = std::make_unique<Artwork>(ui, 64.0f, 14.0f);
    m_art->SetPosition(m_pad, top);
    m_layer.Children().InsertAtTop(m_art->Visual());
    const float art = m_art->Size();

    // Previous, play/pause (a little bigger) and next, on the right.
    const float sideButton = ui.EvenPx(36.0f);
    const float playButton = ui.EvenPx(42.0f);
    const float gap = ui.Px(2.0f);
    const float right = m_width - m_pad + ui.Px(4.0f);  // the highlight circles may reach into the margin
    const float nextX = right - sideButton;
    const float playX = nextX - gap - playButton;
    const float previousX = playX - gap - sideButton;
    const float middle = top + art / 2.0f;
    m_previous = std::make_unique<IconButton>(ui, Glyph::Previous, 36.0f, 15.0f);
    m_playPause = std::make_unique<IconButton>(ui, m_media.playing ? Glyph::Pause : Glyph::Play, 42.0f, 20.0f);
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 36.0f, 15.0f);
    m_previous->SetBounds(previousX, std::round(middle - sideButton / 2.0f), sideButton, sideButton);
    m_playPause->SetBounds(playX, std::round(middle - playButton / 2.0f), playButton, playButton);
    m_next->SetBounds(nextX, std::round(middle - sideButton / 2.0f), sideButton, sideButton);
    m_previous->onClick = [this] { if (m_actions.previous) m_actions.previous(); };
    m_playPause->onClick = [this] { if (m_actions.playPause) m_actions.playPause(); };
    m_next->onClick = [this] { if (m_actions.next) m_actions.next(); };
    AddControl(*m_previous);
    AddControl(*m_playPause);
    AddControl(*m_next);

    // Title, artist and the app it's playing in.
    m_textX = m_pad + art + ui.Px(14.0f);
    m_textWidth = previousX - ui.Px(6.0f) - m_textX;
    m_title = std::make_unique<Label>(ui, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_artist = std::make_unique<Label>(ui, 12.5f);
    m_artist->Visual().Opacity(0.65f);
    m_title->SetMaxWidth(m_textWidth);
    m_artist->SetMaxWidth(m_textWidth);
    m_layer.Children().InsertAtTop(m_title->Visual());
    m_layer.Children().InsertAtTop(m_artist->Visual());
    m_app = std::make_unique<TextButton>(ui, 11.0f);
    m_app->SetTextOpacity(0.5f);
    m_app->onClick = [this] { if (m_actions.nextMediaSession) m_actions.nextMediaSession(); };
    AddControl(*m_app);

    // The progress line under the row.
    const float progressY = top + art + ui.Px(14.0f);
    m_progress = std::make_unique<ProgressBar>(ui);
    m_progress->SetBounds(m_pad, progressY, m_width - 2.0f * m_pad, ui.Px(4.0f));
    m_layer.Children().InsertAtTop(m_progress->Visual());

    ApplyMedia(false);
    return progressY + ui.Px(4.0f);
}

void ControlCenter::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        ApplyMedia(Visible());  // hidden content never animates: see View::Retire
    }
}

void ControlCenter::ApplyMedia(bool animate)
{
    const bool has = m_media.hasSession;
    m_title->SetText(!has ? L"Nothing playing" : m_media.title.empty() ? m_media.app : m_media.title);
    m_artist->SetText(!has ? L"Play music or a video and it shows up here" : m_media.artist);

    // The app name switches apps when several are playing media (PLAN.md E1).
    const bool several = m_media.sessionCount > 1;
    m_app->SetText(has ? (several ? m_media.app + L"  \x203A" : m_media.app) : L"", m_textWidth + m_ui.Px(12.0f));
    m_app->SetInteractive(several);

    // Stack the text lines, centered next to the artwork.
    const float appHeight = has ? m_app->Height() : 0.0f;
    const float block = m_title->Height() + m_artist->Height() + appHeight;
    const float textTop = std::round(m_artTop + (m_art->Size() - block) / 2.0f);
    m_title->SetPosition(m_textX, textTop);
    m_artist->SetPosition(m_textX, textTop + m_title->Height());
    m_app->SetBounds(m_textX - m_ui.Px(6.0f), textTop + m_title->Height() + m_artist->Height(), m_app->Width(), m_app->Height());

    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
    m_playPause->SetGlyph(m_media.playing ? Glyph::Pause : Glyph::Play, /*morph*/ true, animate);
    m_previous->SetEnabled(has && m_media.canPrevious);
    m_playPause->SetEnabled(has && m_media.canPlayPause);
    m_next->SetEnabled(has && m_media.canNext);
    UpdateProgress();
}

void ControlCenter::UpdateProgress()
{
    // Hidden when the app doesn't say where the song is (PLAN.md E3).
    const bool show = m_media.hasSession && m_media.hasTimeline;
    m_progress->Visual().Opacity(show ? 1.0f : 0.0f);
    if (!show)
    {
        m_progress->Freeze();
        return;
    }
    const double position = m_media.PositionAt(winrt::clock::now());
    const double left = (m_media.durationSeconds - position) / std::max(0.01, m_media.rate);
    // It only glides while the panel can be seen: no frames drawn while idle.
    m_progress->Show(position / m_media.durationSeconds, m_media.playing && Visible(), left);
}

void ControlCenter::OnVisibilityChanged(bool visible)
{
    if (!m_progress)
    {
        return;
    }
    if (visible)
    {
        UpdateProgress();
    }
    else
    {
        m_progress->Freeze();
    }
}

// --- Quick controls ----------------------------------------------------------

float ControlCenter::BuildSlider(float top, std::unique_ptr<Label>& caption, std::unique_ptr<IconButton>& icon,
                                 wchar_t glyph, std::unique_ptr<Slider>& slider)
{
    // A caption above the slider, an icon to its left.
    const Ui& ui = m_ui;
    const float iconSize = ui.EvenPx(32.0f);
    const float sliderX = m_pad + iconSize + ui.Px(10.0f);
    const float sliderHeight = ui.Px(24.0f);
    caption = std::make_unique<Label>(ui, 11.0f);
    caption->SetMaxWidth(m_width - m_pad - sliderX);
    caption->Visual().Opacity(0.55f);
    caption->SetText(L" ");
    caption->SetPosition(sliderX, top);
    m_layer.Children().InsertAtTop(caption->Visual());

    const float lineY = top + caption->Height() + ui.Px(1.0f);
    slider = std::make_unique<Slider>(ui);
    slider->SetBounds(sliderX, lineY, m_width - m_pad - sliderX, sliderHeight);
    icon = std::make_unique<IconButton>(ui, glyph, 32.0f, 15.0f);
    icon->SetBounds(m_pad, lineY + std::round((sliderHeight - iconSize) / 2.0f), iconSize, iconSize);
    AddControl(*icon);
    AddControl(*slider);
    return lineY + sliderHeight + ui.Px(8.0f);
}

float ControlCenter::BuildToggles(float top)
{
    const Ui& ui = m_ui;
    const float gap = ui.Px(10.0f);
    const float tileWidth = std::floor((m_width - 2.0f * m_pad - gap) / 2.0f);
    const float tileHeight = ui.Px(58.0f);
    m_wifi = std::make_unique<ToggleTile>(ui, Glyph::Wifi, L"Wi-Fi");
    m_bluetooth = std::make_unique<ToggleTile>(ui, Glyph::Bluetooth, L"Bluetooth");
    m_wifi->SetBounds(m_pad, top, tileWidth, tileHeight);
    m_bluetooth->SetBounds(m_pad + tileWidth + gap, top, tileWidth, tileHeight);
    m_wifi->onToggle = [this](bool on) { if (m_actions.setWifi) m_actions.setWifi(on); };
    m_bluetooth->onToggle = [this](bool on) { if (m_actions.setBluetooth) m_actions.setBluetooth(on); };
    m_wifi->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:network-wifi"); };
    m_bluetooth->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:bluetooth"); };
    AddControl(*m_wifi);
    AddControl(*m_bluetooth);
    return top + tileHeight;
}

void ControlCenter::SetAudio(const AudioState& audio)
{
    m_audio = audio;
    if (m_layer)
    {
        ApplyAudio(Visible());
    }
}

void ControlCenter::ApplyAudio(bool animate)
{
    std::wstring caption = m_audio.available ? (m_audio.device.empty() ? L"Volume" : m_audio.device) : L"No sound output";
    if (m_audio.available && m_audio.muted)
    {
        caption += L" · muted";
    }
    m_volumeCaption->SetText(caption);
    m_volumeIcon->SetGlyph(VolumeGlyph(m_audio), /*morph*/ false, animate);
    m_volumeIcon->SetEnabled(m_audio.available);
    m_volume->SetEnabled(m_audio.available);
    // Volume keys and Quick Settings glide the slider; it ignores this while
    // you drag it, so it can't jitter (PLAN.md D9).
    m_volume->SetValue(m_audio.muted ? 0.0f : m_audio.volume, animate);
}

bool ControlCenter::SetBrightness(const BrightnessState& brightness)
{
    const bool same = SameDisplays(brightness, m_brightness);
    m_brightness = brightness;
    if (!same)
    {
        return true;  // the caller rebuilds the panel
    }
    if (m_layer)
    {
        ApplyBrightness(Visible());
    }
    return false;
}

void ControlCenter::ApplyBrightness(bool animate)
{
    for (size_t i = 0; i < m_brightnessRows.size() && i < m_brightness.displays.size(); ++i)
    {
        const DisplayBrightness& display = m_brightness.displays[i];
        BrightnessRow& row = m_brightnessRows[i];
        // A monitor that doesn't answer gets a hint instead (PLAN.md E8).
        row.caption->SetText(display.supported ? display.name : display.name + L" · turn on DDC/CI in its menu to control it");
        row.slider->SetEnabled(display.supported);
        row.icon->SetEnabled(display.supported);
        if (display.supported)
        {
            row.slider->SetValue(display.value / 100.0f, animate);
        }
    }
}

void ControlCenter::SetRadios(const RadiosState& radios)
{
    m_radios = radios;
    if (m_layer)
    {
        ApplyRadios(Visible());
    }
}

void ControlCenter::ApplyRadios(bool animate)
{
    auto apply = [&](ToggleTile& tile, const RadioInfo& radio)
    {
        tile.SetOn(radio.on, animate);
        tile.SetSubtitle(radio.detail);
        tile.SetBusy(radio.busy);
        tile.SetEnabled(radio.available && radio.switchable);
    };
    apply(*m_wifi, m_radios.wifi);
    apply(*m_bluetooth, m_radios.bluetooth);
}
