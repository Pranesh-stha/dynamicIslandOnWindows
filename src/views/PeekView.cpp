#include "views/PeekView.h"

#include <algorithm>
#include <chrono>
#include <format>

#include "platform/Settings.h"
#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;
using winrt::Windows::UI::Color;

namespace
{
    // Two lines of text, centered as a block in the strip.
    void PlaceLines(Label& top, Label& bottom, float x, float height)
    {
        const float textTop = std::round((height - top.Height() - bottom.Height()) / 2.0f);
        top.SetPosition(x, textTop);
        bottom.SetPosition(x, textTop + top.Height());
    }

    // The glance's badge for each part of the day: a sky gradient (top left
    // to bottom right) with the sun, or the moon at night.
    struct Sky
    {
        Color from, to;
        wchar_t glyph;
    };

    Sky SkyFor(TimeOfDay time)
    {
        switch (time)
        {
        case TimeOfDay::Dawn: return { { 255, 255, 183, 120 }, { 255, 236, 104, 150 }, Glyph::Brightness };  // peach to pink
        case TimeOfDay::Day: return { { 255, 255, 196, 52 }, { 255, 245, 118, 22 }, Glyph::Brightness };     // gold to orange
        case TimeOfDay::Dusk: return { { 255, 255, 118, 92 }, { 255, 150, 76, 196 }, Glyph::Brightness };    // coral to purple
        default: return { { 255, 88, 96, 222 }, { 255, 36, 28, 104 }, Glyph::Sleep };                        // indigo night, the moon
        }
    }

    Color BatteryColor(BatteryTone tone)
    {
        switch (tone)
        {
        case BatteryTone::Charging: return { 255, 52, 199, 89 };  // green, as in the charging moment
        case BatteryTone::Low: return { 255, 255, 159, 10 };
        case BatteryTone::Critical: return { 255, 255, 69, 58 };
        default: return White(0.92f);
        }
    }
}

TimeOfDay TimeOfDayAt(int hour)
{
    if (hour >= 5 && hour < 8)
    {
        return TimeOfDay::Dawn;
    }
    if (hour >= 8 && hour < 17)
    {
        return TimeOfDay::Day;
    }
    if (hour >= 17 && hour < 20)
    {
        return TimeOfDay::Dusk;
    }
    return TimeOfDay::Night;
}

BatteryTone BatteryToneOf(const PowerState& power)
{
    if (power.charging || power.pluggedIn)
    {
        return BatteryTone::Charging;
    }
    if (power.percent <= 10)
    {
        return BatteryTone::Critical;
    }
    return power.percent <= 20 ? BatteryTone::Low : BatteryTone::Normal;
}

void PeekView::Build()
{
    const Ui& ui = m_ui;
    const LayoutSettings& layout = *ui.layout;
    ResetLayer(ui.EvenPx(layout.peekWidth), ui.Px(layout.peekHeight));
    auto container = [&]
    {
        auto visual = ui.compositor.CreateContainerVisual();
        visual.Size({ m_width, m_height });
        m_layer.Children().InsertAtTop(visual);
        return visual;
    };

    // Nothing playing: a glance. A badge for the part of the day (the sun on
    // a dawn, day or dusk sky, the moon at night), the time in big rolling
    // digits with the date under it, and a battery that fills up as Peek
    // opens, with the percentage and how long it lasts.
    m_glance = container();
    const float pad = ui.Px(10.0f);
    const float badge = ui.EvenPx(36.0f);
    const float badgeY = std::round((m_height - badge) / 2.0f);
    m_sky = ui.compositor.CreateContainerVisual();
    m_sky.Size({ badge, badge });
    m_sky.Offset({ pad, badgeY, 0.0f });
    m_sky.CenterPoint({ badge / 2.0f, badge / 2.0f, 0.0f });  // pops in from its middle
    m_skyCircle = std::make_unique<RoundedRect>(ui, badge, badge, badge / 2.0f, White(0.14f));
    m_sky.Children().InsertAtTop(m_skyCircle->visual);
    m_skyIcon = std::make_unique<Label>(ui, 17.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_sky.Children().InsertAtTop(m_skyIcon->Visual());
    m_glance.Children().InsertAtTop(m_sky);
    m_skyShown = -1;
    m_clock = std::make_unique<RollingText>(ui, 19.0f);
    m_glance.Children().InsertAtTop(m_clock->Visual());
    m_date = std::make_unique<Label>(ui, 11.0f);
    m_date->Visual().Opacity(0.6f);
    m_glance.Children().InsertAtTop(m_date->Visual());

    // The battery, drawn: an outline, a fill that grows from the left, the
    // tip, and a bolt while charging.
    const float bodyWidth = ui.EvenPx(26.0f), bodyHeight = ui.EvenPx(13.0f);
    const float edge = std::max(1.0f, ui.Px(2.0f));
    m_battery = ui.compositor.CreateContainerVisual();
    m_battery.Size({ bodyWidth + std::max(1.0f, ui.Px(1.0f)) + ui.Px(2.0f), bodyHeight });
    m_batteryBody = std::make_unique<RoundedRect>(ui, bodyWidth, bodyHeight, ui.Px(4.0f), White(0.22f));
    m_batteryFill = std::make_unique<RoundedRect>(ui, bodyWidth - 2.0f * edge, bodyHeight - 2.0f * edge, ui.Px(2.5f), White(0.92f));
    m_batteryFill->visual.Offset({ edge, edge, 0.0f });
    m_batteryFill->visual.CenterPoint({ 0.0f, 0.0f, 0.0f });  // fills from the left
    m_batteryTip = std::make_unique<RoundedRect>(ui, ui.Px(2.0f), std::round(bodyHeight * 0.45f), ui.Px(1.0f), White(0.22f));
    m_batteryTip->visual.Offset({ bodyWidth + std::max(1.0f, ui.Px(1.0f)), std::round(bodyHeight * 0.275f), 0.0f });
    m_bolt = std::make_unique<Label>(ui, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_bolt->SetText(std::wstring(1, Glyph::Bolt));
    m_bolt->SetPosition(std::round((bodyWidth - m_bolt->Width()) / 2.0f), std::round((bodyHeight - m_bolt->Height()) / 2.0f));
    for (const wuc::Visual& visual : { wuc::Visual(m_batteryBody->visual), wuc::Visual(m_batteryFill->visual),
                                       wuc::Visual(m_batteryTip->visual), wuc::Visual(m_bolt->Visual()) })
    {
        m_battery.Children().InsertAtTop(visual);
    }
    m_glance.Children().InsertAtTop(m_battery);
    m_batteryPercent = std::make_unique<RollingText>(ui, 14.0f);
    m_glance.Children().InsertAtTop(m_batteryPercent->Visual());
    m_batteryDetail = std::make_unique<Label>(ui, 11.0f);
    m_batteryDetail->SetMaxWidth(ui.Px(150.0f));
    m_batteryDetail->Visual().Opacity(0.6f);
    m_glance.Children().InsertAtTop(m_batteryDetail->Visual());
    UpdateClock(false);
    ApplyBattery(false);

    // Playing: artwork, song, play/pause and next.
    m_playing = container();
    const float artInset = std::round((m_height - ui.EvenPx(layout.peekHeight - 16.0f)) / 2.0f);
    m_art = std::make_unique<Artwork>(ui, layout.peekHeight - 16.0f, 10.0f);
    m_art->SetPosition(artInset, artInset);
    m_playing.Children().InsertAtTop(m_art->Visual());

    const float button = ui.EvenPx(34.0f);
    const float buttonY = std::round((m_height - button) / 2.0f);
    const float nextX = m_width - ui.Px(10.0f) - button;
    const float playX = nextX - ui.Px(2.0f) - button;
    m_playPause = std::make_unique<IconButton>(ui, m_media.playing ? Glyph::Pause : Glyph::Play, 34.0f, 16.0f);
    m_playPause->SetBounds(playX, buttonY, button, button);
    m_playPause->onClick = [this] { if (m_actions.playPause) m_actions.playPause(); };
    m_next = std::make_unique<IconButton>(ui, Glyph::Next, 34.0f, 14.0f);
    m_next->SetBounds(nextX, buttonY, button, button);
    m_next->onClick = [this] { if (m_actions.next) m_actions.next(); };
    for (IconButton* control : { m_playPause.get(), m_next.get() })
    {
        m_playing.Children().InsertAtTop(control->Visual());
        m_controls.Add(*control);
    }

    const float textX = artInset + m_art->Size() + ui.Px(10.0f);
    const float textWidth = playX - ui.Px(6.0f) - textX;
    m_title = std::make_unique<Label>(ui, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_title->SetMaxWidth(textWidth);
    m_subtitle = std::make_unique<Label>(ui, 11.5f);
    m_subtitle->SetMaxWidth(textWidth);
    m_subtitle->Visual().Opacity(0.6f);
    m_playing.Children().InsertAtTop(m_title->Visual());
    m_playing.Children().InsertAtTop(m_subtitle->Visual());

    // Timer: the icon in an accent circle, the time big, what it is under
    // it, and pause/resume and stop on the right.
    m_timerLayer = container();
    m_timerBadge = std::make_unique<RoundedRect>(ui, badge, badge, badge / 2.0f, ui.accent);
    m_timerBadge->visual.Offset({ pad, badgeY, 0.0f });
    m_timerLayer.Children().InsertAtTop(m_timerBadge->visual);
    m_timerIcon = std::make_unique<Label>(ui, 16.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_timerIcon->SetText(std::wstring(1, Glyph::Stopwatch));
    m_timerIcon->SetPosition(pad + std::round((badge - m_timerIcon->Width()) / 2.0f), badgeY + std::round((badge - m_timerIcon->Height()) / 2.0f));
    m_timerLayer.Children().InsertAtTop(m_timerIcon->Visual());
    m_timerTime = std::make_unique<RollingText>(ui, 19.0f);
    m_timerLayer.Children().InsertAtTop(m_timerTime->Visual());
    m_timerCaption = std::make_unique<Label>(ui, 11.0f);
    m_timerCaption->Visual().Opacity(0.6f);
    m_timerLayer.Children().InsertAtTop(m_timerCaption->Visual());
    m_timerPause = std::make_unique<IconButton>(ui, Glyph::Pause, 34.0f, 16.0f);
    m_timerPause->SetBounds(playX, buttonY, button, button);
    m_timerPause->onClick = [this] { if (m_actions.pauseResumeTimer) m_actions.pauseResumeTimer(); };
    m_timerStop = std::make_unique<IconButton>(ui, Glyph::Cancel, 34.0f, 13.0f);
    m_timerStop->SetBounds(nextX, buttonY, button, button);
    m_timerStop->onClick = [this] { if (m_actions.stopTimer) m_actions.stopTimer(); };
    for (IconButton* control : { m_timerPause.get(), m_timerStop.get() })
    {
        m_timerLayer.Children().InsertAtTop(control->Visual());
        m_controls.Add(*control);
    }

    Apply(false);
}

void PeekView::SetTimer(const TimerState& timer, const std::wstring& text)
{
    m_timer = timer;
    m_timerText = text;
    if (m_layer)
    {
        Apply(Visible());
    }
}

void PeekView::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        Apply(Visible());  // hidden content never animates: see View::Retire
    }
}

void PeekView::SetPower(const PowerState& power)
{
    m_power = power;
    if (m_glance)
    {
        ApplyBattery(Visible() && ShowsGlance());  // only what's on screen animates
    }
}

bool PeekView::ShowsGlance() const
{
    return m_timer.mode == TimerMode::None && !m_media.hasSession;
}

void PeekView::OnVisibilityChanged(bool visible)
{
    if (!m_glance)
    {
        return;
    }
    if (visible)
    {
        UpdateClock(false);  // as it is now, without rolling
        ApplyBattery(false);
        ScheduleTick();
        return;
    }
    if (m_tick)
    {
        m_tick.Stop();
    }
    // Nothing animates while hidden: finish the entrance at once.
    m_sky.StopAnimation(L"Scale");
    m_sky.Scale({ 1.0f, 1.0f, 1.0f });
    ApplyBattery(false);
}

void PeekView::OnAppear()
{
    if (!m_glance || !ShowsGlance())
    {
        return;
    }
    // A little entrance: the badge pops in, the battery fills up to its charge.
    const MotionSettings& motion = *m_ui.motion;
    m_sky.StopAnimation(L"Scale");
    m_sky.Scale({ 0.55f, 0.55f, 1.0f });
    Motion::SpringTo(m_sky, L"Scale", Motion::float3{ 1.0f, 1.0f, 1.0f }, motion.toggle, motion.contentFadeInDelayMs);
    if (m_power.hasBattery)
    {
        const float level = std::clamp(m_power.percent / 100.0f, 0.04f, 1.0f);
        m_batteryFill->visual.StopAnimation(L"Scale");
        m_batteryFill->visual.Scale({ 0.0f, 1.0f, 1.0f });
        Motion::EaseTo(m_batteryFill->visual, L"Scale", Motion::float3{ level, 1.0f, 1.0f }, 650, motion.contentFadeInDelayMs + 60);
    }
}

void PeekView::UpdateClock(bool animate)
{
    // In your own formats (Settings > Time & language): "8:42 PM" or "20:42"; "Tue, 3 Nov".
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t time[64]{};
    wchar_t date[64]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, ARRAYSIZE(time));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"ddd, d MMM", date, ARRAYSIZE(date), nullptr);
    m_clock->SetText(time, animate);  // the minute rolls over like a counter
    m_date->SetText(date);
    const float x = m_sky.Offset().x + m_sky.Size().x + m_ui.Px(10.0f);
    const float top = std::round((m_height - m_clock->Height() - m_date->Height()) / 2.0f);
    m_clock->SetPosition(x, top);
    m_date->SetPosition(x, top + m_clock->Height());

    const TimeOfDay part = TimeOfDayAt(now.wHour);
    if (static_cast<int>(part) == m_skyShown)
    {
        return;
    }
    m_skyShown = static_cast<int>(part);
    const Sky sky = SkyFor(part);
    auto gradient = m_ui.compositor.CreateLinearGradientBrush();
    gradient.StartPoint({ 0.0f, 0.0f });
    gradient.EndPoint({ 1.0f, 1.0f });
    gradient.ColorStops().Append(m_ui.compositor.CreateColorGradientStop(0.0f, sky.from));
    gradient.ColorStops().Append(m_ui.compositor.CreateColorGradientStop(1.0f, sky.to));
    m_skyCircle->shape.FillBrush(gradient);
    m_skyIcon->SetText(std::wstring(1, sky.glyph));
    const float badge = m_sky.Size().x;
    m_skyIcon->SetPosition(std::round((badge - m_skyIcon->Width()) / 2.0f), std::round((badge - m_skyIcon->Height()) / 2.0f));
}

void PeekView::ScheduleTick()
{
    if (!m_tick)
    {
        m_tick = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_tick.IsRepeating(false);
        m_tick.Tick([this](auto&&, auto&&)
        {
            if (m_visible)
            {
                UpdateClock(ShowsGlance());  // rolls only while the glance is on screen
                ScheduleTick();
            }
        });
    }
    // Just after the next whole minute; only while Peek is on screen.
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const int untilNextMinute = (60 - now.wSecond) * 1000 - now.wMilliseconds + 50;
    m_tick.Stop();
    m_tick.Interval(std::chrono::milliseconds(untilNextMinute));
    m_tick.Start();
}

void PeekView::ApplyBattery(bool animate)
{
    // A desktop PC without a battery: just the time and date.
    const float shown = m_power.hasBattery ? 1.0f : 0.0f;
    m_battery.Opacity(shown);
    m_batteryPercent->Visual().Opacity(shown);
    m_batteryDetail->Visual().Opacity(0.6f * shown);
    if (!m_power.hasBattery)
    {
        return;
    }

    m_batteryFill->shape.FillBrush(m_ui.compositor.CreateColorBrush(BatteryColor(BatteryToneOf(m_power))));
    m_bolt->Visual().Opacity(m_power.charging ? 1.0f : 0.0f);
    const float level = std::clamp(m_power.percent / 100.0f, 0.04f, 1.0f);
    if (animate)
    {
        Motion::EaseTo(m_batteryFill->visual, L"Scale", Motion::float3{ level, 1.0f, 1.0f }, 450);
    }
    else
    {
        m_batteryFill->visual.StopAnimation(L"Scale");
        m_batteryFill->visual.Scale({ level, 1.0f, 1.0f });
    }
    m_batteryPercent->SetText(std::format(L"{} %", m_power.percent), animate);
    m_batteryDetail->SetText(PowerDescription(m_power));

    // Right-aligned: [84 %] [battery] on the time's line, how long it lasts under it.
    const float right = m_width - m_ui.Px(14.0f);
    const float top = std::round((m_height - m_clock->Height() - m_date->Height()) / 2.0f);
    const float middle = top + m_clock->Height() / 2.0f;
    const float batteryX = right - m_battery.Size().x;
    m_battery.Offset({ batteryX, std::round(middle - m_battery.Size().y / 2.0f), 0.0f });
    m_batteryPercent->SetPosition(batteryX - m_ui.Px(6.0f) - m_batteryPercent->Width(), std::round(middle - m_batteryPercent->Height() / 2.0f));
    m_batteryDetail->SetPosition(right - m_batteryDetail->Width(), top + m_clock->Height());
}

void PeekView::Apply(bool animate)
{
    const bool timer = m_timer.mode != TimerMode::None;
    const bool has = m_media.hasSession;
    auto fade = [&](const winrt::Windows::UI::Composition::Visual& visual, float opacity)
    {
        if (animate)
        {
            Motion::EaseTo(visual, L"Opacity", opacity, 200);
        }
        else
        {
            visual.StopAnimation(L"Opacity");
            visual.Opacity(opacity);
        }
    };
    fade(m_glance, ShowsGlance() ? 1.0f : 0.0f);
    fade(m_playing, !timer && has ? 1.0f : 0.0f);
    fade(m_timerLayer, timer ? 1.0f : 0.0f);
    // Only the buttons on show take the mouse.
    m_playPause->SetInteractive(!timer);
    m_next->SetInteractive(!timer);
    m_timerPause->SetInteractive(timer && !m_timer.finished);
    m_timerStop->SetInteractive(timer);

    if (timer)
    {
        const float textX = m_timerBadge->visual.Offset().x + m_timerBadge->visual.Size().x + m_ui.Px(10.0f);
        m_timerTime->SetText(m_timer.finished ? L"0:00" : m_timerText, animate);
        m_timerCaption->SetText(m_timer.finished ? L"Time's up"
                              : !m_timer.running ? L"Paused"
                              : m_timer.mode == TimerMode::Countdown ? L"Timer" : L"Stopwatch");
        const float block = m_timerTime->Height() + m_timerCaption->Height();
        const float top = std::round((m_height - block) / 2.0f);
        m_timerTime->SetPosition(textX, top);
        m_timerCaption->SetPosition(textX, top + m_timerTime->Height());
        m_timerPause->SetGlyph(m_timer.running ? Glyph::Pause : Glyph::Play, /*morph*/ true, animate);
        m_timerPause->SetEnabled(!m_timer.finished);
    }

    m_playPause->SetEnabled(has && m_media.canPlayPause);
    m_next->SetEnabled(has && m_media.canNext);
    if (!has)
    {
        return;
    }

    m_title->SetText(m_media.title.empty() ? m_media.app : m_media.title);
    std::wstring subtitle = m_media.artist;
    if (!m_media.title.empty())
    {
        subtitle += subtitle.empty() ? m_media.app : L" · " + m_media.app;
    }
    m_subtitle->SetText(subtitle);
    const float textX = m_art->Visual().Offset().x + m_art->Size() + m_ui.Px(10.0f);
    PlaceLines(*m_title, *m_subtitle, textX, m_height);
    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);
    m_playPause->SetGlyph(m_media.playing ? Glyph::Pause : Glyph::Play, /*morph*/ true, animate);
}
