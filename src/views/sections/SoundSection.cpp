#include "views/sections/SoundSection.h"

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace
{
    wchar_t VolumeGlyph(float volume, bool muted)
    {
        if (muted)
        {
            return Glyph::Mute;
        }
        return volume < 0.01f ? Glyph::Volume0 : volume < 0.34f ? Glyph::Volume1 : volume < 0.67f ? Glyph::Volume2 : Glyph::Volume3;
    }
}

float SoundSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    y = AddTitle(layer, L"Sound", x, y);

    // The main volume. The speaker icon mutes; right-click opens the sound settings.
    y = m_master.Build(m_ui, layer, controls, x, y, width, Glyph::Volume3);
    m_master.icon->onClick = [this] { if (m_actions.toggleMute) m_actions.toggleMute(); };
    m_master.slider->onChange = [this](float value, bool) { if (m_actions.setVolume) m_actions.setVolume(value); };
    auto soundSettings = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:sound"); };
    m_master.slider->onRightClick = soundSettings;
    m_master.icon->onRightClick = soundSettings;

    // The mixer: one row per app. Rows are made once and filled as apps come and go.
    const float mixerTop = y;
    for (int i = 0; i < kAppRows; ++i)
    {
        SliderRow& row = m_apps[i];
        y = row.Build(m_ui, layer, controls, x, y, width, Glyph::Volume3);
        row.icon->onClick = [this, i] { if (m_actions.toggleAppMute && !m_rowKeys[i].empty()) m_actions.toggleAppMute(m_rowKeys[i]); };
        row.slider->onChange = [this, i](float value, bool)
        {
            if (m_actions.setAppVolume && !m_rowKeys[i].empty())
            {
                m_actions.setAppVolume(m_rowKeys[i], value);
            }
        };
        row.slider->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:apps-volume"); };
    }
    m_hint = std::make_unique<Label>(m_ui, 12.0f);
    m_hint->SetMaxWidth(width);
    m_hint->SetText(L"Apps playing sound show up here");
    m_hint->Visual().Opacity(0.0f);
    m_hint->SetPosition(x, mixerTop + m_ui.Px(6.0f));
    layer.Children().InsertAtTop(m_hint->Visual());

    Apply(false);
    return y;
}

void SoundSection::SetAudio(const AudioState& audio, bool animate)
{
    m_audio = audio;
    if (m_master.slider)
    {
        Apply(animate);
    }
}

void SoundSection::AssignRows()
{
    // While you drag one of the app sliders, rows must not move under the mouse.
    for (const SliderRow& row : m_apps)
    {
        if (row.slider->Dragging())
        {
            return;
        }
    }
    std::vector<std::wstring> keys;
    for (const AppVolume& app : m_audio.apps)
    {
        keys.push_back(app.key);
    }
    AssignMixerRows(m_rowKeys, keys);
}

void SoundSection::Apply(bool animate)
{
    // Main volume.
    std::wstring caption = m_audio.available ? (m_audio.device.empty() ? L"Volume" : m_audio.device) : L"No sound output";
    if (m_audio.available && m_audio.muted)
    {
        caption += L" · muted";
    }
    m_master.caption->SetText(caption);
    m_master.icon->SetGlyph(VolumeGlyph(m_audio.volume, m_audio.muted || !m_audio.available), false, animate);
    m_master.icon->SetEnabled(m_audio.available);
    m_master.slider->SetEnabled(m_audio.available);
    // Volume keys and Quick Settings glide the slider; it ignores this while
    // you drag it, so it can't jitter (PLAN.md D9).
    m_master.slider->SetValue(m_audio.muted ? 0.0f : m_audio.volume, animate);

    // The mixer.
    AssignRows();
    bool any = false;
    for (int i = 0; i < kAppRows; ++i)
    {
        SliderRow& row = m_apps[i];
        const auto app = std::find_if(m_audio.apps.begin(), m_audio.apps.end(),
            [&](const AppVolume& entry) { return !m_rowKeys[i].empty() && entry.key == m_rowKeys[i]; });
        if (app == m_audio.apps.end())
        {
            row.SetShown(false);
            continue;
        }
        any = true;
        row.SetShown(true);
        std::wstring name = app->name;
        if (app->muted)
        {
            name += L" · muted";
        }
        row.caption->SetText(name);
        row.icon->SetGlyph(VolumeGlyph(app->volume, app->muted), false, animate);
        row.slider->SetValue(app->muted ? 0.0f : app->volume, animate);
    }
    m_hint->Visual().Opacity(any ? 0.0f : 0.5f);
}
