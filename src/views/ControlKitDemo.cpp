#include "views/ControlKitDemo.h"

#include <chrono>

#include "platform/Log.h"
#include "platform/Settings.h"
#include "render/Glyphs.h"

namespace
{
    // A toggle that pretends Windows takes a moment to switch the radio, to
    // show the "working" pulse (PLAN.md D8).
    winrt::Windows::System::DispatcherQueueTimer FakeSwitchDelay(ToggleTile& tile)
    {
        auto timer = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.Interval(std::chrono::milliseconds(900));
        timer.IsRepeating(false);
        timer.Tick([&tile](auto&&, auto&&)
        {
            tile.SetBusy(false);
            tile.SetSubtitle(tile.IsOn() ? L"On" : L"Off");
        });
        return timer;
    }
}

void ControlKitDemo::Build()
{
    const Ui& ui = m_ui;
    const float width = ui.EvenPx(400.0f);  // a narrow panel of its own
    ResetLayer(width, 0.0f);
    const float pad = ui.Px(18.0f);
    auto add = [&](const winrt::Windows::UI::Composition::Visual& visual) { m_layer.Children().InsertAtTop(visual); };
    float y = pad;

    // Row 1: a stand-in for "now playing".
    const float art = ui.EvenPx(56.0f);
    m_art = std::make_unique<RoundedRect>(ui, art, art, ui.Px(12.0f), White(0.12f));
    m_art->visual.Offset({ pad, y, 0.0f });
    add(m_art->visual);
    m_artIcon = std::make_unique<Label>(ui, 22.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_artIcon->SetText(std::wstring(1, Glyph::MusicNote));
    m_artIcon->SetPosition(pad + std::round((art - m_artIcon->Width()) / 2.0f), y + std::round((art - m_artIcon->Height()) / 2.0f));
    add(m_artIcon->Visual());

    const float button = ui.EvenPx(36.0f);
    const float gap = ui.Px(2.0f);
    const float buttonsX = width - pad - 3.0f * button - 2.0f * gap;
    const float buttonY = y + std::round((art - button) / 2.0f);
    m_previous = std::make_unique<IconButton>(ui, Glyph::Previous, 36.0f, 15.0f);
    m_playPause = std::make_unique<IconButton>(ui, Glyph::Play, 36.0f, 18.0f);
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 36.0f, 15.0f);
    m_previous->SetBounds(buttonsX, buttonY, button, button);
    m_playPause->SetBounds(buttonsX + button + gap, buttonY, button, button);
    m_next->SetBounds(buttonsX + 2.0f * (button + gap), buttonY, button, button);
    m_playPause->onClick = [this]
    {
        m_playing = !m_playing;
        m_playPause->SetGlyph(m_playing ? Glyph::Pause : Glyph::Play, /*morph*/ true);
    };
    m_previous->onClick = [] { Log::Info(L"demo: previous"); };
    m_next->onClick = [] { Log::Info(L"demo: next"); };

    const float textX = pad + art + ui.Px(12.0f);
    const float textWidth = buttonsX - ui.Px(8.0f) - textX;
    m_title = std::make_unique<Label>(ui, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_title->SetMaxWidth(textWidth);
    m_title->SetText(L"Control kit");
    m_subtitle = std::make_unique<Label>(ui, 12.5f);
    m_subtitle->SetMaxWidth(textWidth);
    m_subtitle->SetText(L"Every control animates");
    m_subtitle->Visual().Opacity(0.6f);
    const float textTop = y + std::round((art - m_title->Height() - m_subtitle->Height()) / 2.0f);
    m_title->SetPosition(textX, textTop);
    m_subtitle->SetPosition(textX, textTop + m_title->Height());
    add(m_title->Visual());
    add(m_subtitle->Visual());
    y += art + ui.Px(14.0f);

    // Rows 2-3: sliders with an icon on the left and a caption above.
    const float icon = ui.EvenPx(32.0f);
    const float sliderX = pad + icon + ui.Px(10.0f);
    const float sliderWidth = width - pad - sliderX;
    const float sliderHeight = ui.EvenPx(24.0f);
    auto sliderRow = [&](std::unique_ptr<Label>& caption, const wchar_t* text, std::unique_ptr<IconButton>& iconButton,
                         wchar_t glyph, std::unique_ptr<Slider>& slider, float value)
    {
        caption = std::make_unique<Label>(ui, 11.0f);
        caption->SetText(text);
        caption->Visual().Opacity(0.55f);
        caption->SetPosition(sliderX, y);
        add(caption->Visual());
        const float lineY = y + caption->Height() + ui.Px(1.0f);
        slider = std::make_unique<Slider>(ui);
        slider->SetBounds(sliderX, lineY, sliderWidth, sliderHeight);
        slider->SetValue(value, false);
        iconButton = std::make_unique<IconButton>(ui, glyph, 32.0f, 15.0f);
        iconButton->SetBounds(pad, lineY + std::round((sliderHeight - icon) / 2.0f), icon, icon);
        y = lineY + sliderHeight + ui.Px(8.0f);
    };
    sliderRow(m_volumeCaption, L"Volume (demo)", m_volumeButton, Glyph::Volume3, m_volume, 0.6f);
    sliderRow(m_brightnessCaption, L"Brightness (demo)", m_brightnessIcon, Glyph::Brightness, m_brightness, 0.4f);
    m_volume->onChange = [this](float, bool) { UpdateVolumeIcon(); };
    m_volumeButton->onClick = [this]
    {
        m_muted = !m_muted;
        UpdateVolumeIcon();
    };
    m_brightness->onChange = [](float value, bool final)
    {
        if (final)
        {
            Log::Info(L"demo: brightness {:.0f} %", value * 100.0f);
        }
    };
    y += ui.Px(4.0f);

    // Row 4: a segmented choice.
    m_mode = std::make_unique<Segmented>(ui, std::vector<std::wstring>{ L"Extend", L"Duplicate", L"Second only" });
    m_mode->SetBounds(pad, y, width - 2.0f * pad, ui.Px(32.0f));
    y += ui.Px(32.0f) + ui.Px(12.0f);

    // Row 5: two toggle tiles.
    const float tileGap = ui.Px(10.0f);
    const float tileWidth = std::floor((width - 2.0f * pad - tileGap) / 2.0f);
    const float tileHeight = ui.Px(58.0f);
    m_wifi = std::make_unique<ToggleTile>(ui, Glyph::Wifi, L"Wi-Fi");
    m_bluetooth = std::make_unique<ToggleTile>(ui, Glyph::Bluetooth, L"Bluetooth");
    m_wifi->SetBounds(pad, y, tileWidth, tileHeight);
    m_bluetooth->SetBounds(pad + tileWidth + tileGap, y, tileWidth, tileHeight);
    m_wifi->SetOn(true, false);
    m_wifi->SetSubtitle(L"On");
    m_bluetooth->SetSubtitle(L"Off");
    m_wifiTimer = FakeSwitchDelay(*m_wifi);
    m_bluetoothTimer = FakeSwitchDelay(*m_bluetooth);
    m_wifi->onToggle = [this](bool)
    {
        m_wifi->SetBusy(true);
        m_wifi->SetSubtitle(L"Switching…");
        m_wifiTimer.Stop();
        m_wifiTimer.Start();
    };
    m_bluetooth->onToggle = [this](bool)
    {
        m_bluetooth->SetBusy(true);
        m_bluetooth->SetSubtitle(L"Switching…");
        m_bluetoothTimer.Stop();
        m_bluetoothTimer.Start();
    };
    y += tileHeight + pad;

    for (Control* control : std::initializer_list<Control*>{ m_previous.get(), m_playPause.get(), m_next.get(),
             m_volumeButton.get(), m_volume.get(), m_brightnessIcon.get(), m_brightness.get(), m_mode.get(),
             m_wifi.get(), m_bluetooth.get() })
    {
        add(control->Visual());
        m_controls.Add(*control);
    }
    SetHeight(y);
}

void ControlKitDemo::UpdateVolumeIcon()
{
    const float level = m_volume->Value();
    const wchar_t glyph = m_muted ? Glyph::Mute
                        : level < 0.01f ? Glyph::Volume0
                        : level < 0.34f ? Glyph::Volume1
                        : level < 0.67f ? Glyph::Volume2
                                        : Glyph::Volume3;
    m_volumeButton->SetGlyph(glyph, false);
}
