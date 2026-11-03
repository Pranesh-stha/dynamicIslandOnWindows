#include "views/PeekView.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

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

    // The glance's sun is warm and glows; the moon is cool and glows less.
    constexpr Color kSun{ 255, 255, 204, 82 };
    constexpr Color kSunGlow{ 70, 255, 204, 82 };
    constexpr Color kMoon{ 255, 226, 232, 255 };
    constexpr Color kMoonGlow{ 40, 226, 232, 255 };

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

SunPath SunPathAt(int minuteOfDay)
{
    // Twelve hours each, every one crossing the arc from left to right.
    constexpr int kSunrise = 6 * 60, kSunset = 18 * 60, kDay = 24 * 60;
    const int minute = (minuteOfDay % kDay + kDay) % kDay;
    if (minute >= kSunrise && minute < kSunset)
    {
        return { true, static_cast<float>(minute - kSunrise) / (kSunset - kSunrise) };
    }
    return { false, static_cast<float>((minute - kSunset + kDay) % kDay) / (kDay - (kSunset - kSunrise)) };
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

    // Nothing playing: a glance. On the left the sun's path: a thin arc over
    // a horizon, with the sun (warm, softly glowing) on it by day, or the moon
    // (cool, among three small stars) by night, as far along as the day or
    // night has got; it rises along the arc as Peek opens. Then the time in
    // big rolling digits with the date under it, and a battery that fills up
    // as Peek opens, with a bolt in it on the charger.
    m_glance = container();
    const float pad = ui.Px(12.0f);
    const float skyWidth = ui.EvenPx(42.0f), skyHeight = ui.Px(30.0f);
    m_arcRadius = ui.Px(16.0f);
    m_arcCenter = { skyWidth / 2.0f, ui.Px(22.0f) };
    m_sky = ui.compositor.CreateContainerVisual();
    m_sky.Size({ skyWidth, skyHeight });
    m_sky.Offset({ pad, std::round((m_height - skyHeight) / 2.0f), 0.0f });
    m_glance.Children().InsertAtTop(m_sky);

    // The arc: a circle's outline, cut off at the horizon.
    auto circle = ui.compositor.CreateEllipseGeometry();
    circle.Center(m_arcCenter);
    circle.Radius({ m_arcRadius, m_arcRadius });
    auto outline = ui.compositor.CreateSpriteShape(circle);
    outline.StrokeBrush(ui.compositor.CreateColorBrush(White(0.28f)));
    outline.StrokeThickness(std::max(1.0f, ui.Px(1.5f)));
    m_arc = ui.compositor.CreateShapeVisual();
    m_arc.Size({ skyWidth, skyHeight });
    m_arc.Shapes().Append(outline);
    m_arc.Clip(ui.compositor.CreateInsetClip(0.0f, 0.0f, 0.0f, skyHeight - m_arcCenter.y));
    m_sky.Children().InsertAtTop(m_arc);
    const float line = std::max(1.0f, ui.Px(1.0f));
    m_horizon = std::make_unique<RoundedRect>(ui, skyWidth, line, line / 2.0f, White(0.28f));
    m_horizon->visual.Offset({ 0.0f, std::round(m_arcCenter.y - line / 2.0f), 0.0f });
    m_sky.Children().InsertAtTop(m_horizon->visual);

    // Stars for the night sky, inside the arc (fractions of its radius).
    m_stars.clear();
    const float stars[][3] = { { -0.5f, -0.42f, 2.0f }, { 0.1f, -0.62f, 1.5f }, { 0.48f, -0.24f, 1.5f } };
    for (const auto& star : stars)
    {
        const float size = std::max(1.0f, ui.Px(star[2]));
        auto dot = std::make_unique<RoundedRect>(ui, size, size, size / 2.0f, White(0.75f));
        dot->visual.Offset({ std::round(m_arcCenter.x + star[0] * m_arcRadius), std::round(m_arcCenter.y + star[1] * m_arcRadius), 0.0f });
        dot->visual.Opacity(0.0f);
        m_sky.Children().InsertAtTop(dot->visual);
        m_stars.push_back(std::move(dot));
    }

    // The sun (or moon): a small disc in a soft glow.
    const float core = ui.EvenPx(8.0f), glow = ui.EvenPx(16.0f);
    m_sun = ui.compositor.CreateContainerVisual();
    m_sun.Size({ glow, glow });
    m_sunGlow = std::make_unique<RoundedRect>(ui, glow, glow, glow / 2.0f, kSunGlow);
    m_sunCore = std::make_unique<RoundedRect>(ui, core, core, core / 2.0f, kSun);
    m_sunCore->visual.Offset({ (glow - core) / 2.0f, (glow - core) / 2.0f, 0.0f });
    m_sun.Children().InsertAtTop(m_sunGlow->visual);
    m_sun.Children().InsertAtTop(m_sunCore->visual);
    m_sky.Children().InsertAtTop(m_sun);
    m_skyDay = -1;

    m_clock = std::make_unique<RollingText>(ui, 19.0f);
    m_glance.Children().InsertAtTop(m_clock->Visual());
    m_date = std::make_unique<Label>(ui, 11.0f);
    m_date->Visual().Opacity(0.6f);
    m_glance.Children().InsertAtTop(m_date->Visual());

    // The battery, drawn: an outline, a fill that grows from the left, the
    // tip, and a bolt on the charger.
    const float bodyWidth = ui.EvenPx(28.0f), bodyHeight = ui.EvenPx(14.0f);
    const float edge = std::max(1.0f, ui.Px(2.0f));
    m_battery = ui.compositor.CreateContainerVisual();
    m_battery.Size({ bodyWidth + std::max(1.0f, ui.Px(1.0f)) + ui.Px(2.0f), bodyHeight });
    m_batteryBody = std::make_unique<RoundedRect>(ui, bodyWidth, bodyHeight, ui.Px(4.0f), White(0.22f));
    m_batteryFill = std::make_unique<RoundedRect>(ui, bodyWidth - 2.0f * edge, bodyHeight - 2.0f * edge, ui.Px(2.5f), White(0.92f));
    m_batteryFill->visual.Offset({ edge, edge, 0.0f });
    m_batteryFill->visual.CenterPoint({ 0.0f, 0.0f, 0.0f });  // fills from the left
    m_batteryTip = std::make_unique<RoundedRect>(ui, ui.Px(2.0f), std::round(bodyHeight * 0.45f), ui.Px(1.0f), White(0.22f));
    m_batteryTip->visual.Offset({ bodyWidth + std::max(1.0f, ui.Px(1.0f)), std::round(bodyHeight * 0.275f), 0.0f });
    m_bolt = std::make_unique<Label>(ui, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
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
    const float timerPad = ui.Px(10.0f);
    const float badge = ui.EvenPx(36.0f);
    const float badgeY = std::round((m_height - badge) / 2.0f);
    m_timerBadge = std::make_unique<RoundedRect>(ui, badge, badge, badge / 2.0f, ui.accent);
    m_timerBadge->visual.Offset({ timerPad, badgeY, 0.0f });
    m_timerLayer.Children().InsertAtTop(m_timerBadge->visual);
    m_timerIcon = std::make_unique<Label>(ui, 16.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_timerIcon->SetText(std::wstring(1, Glyph::Stopwatch));
    m_timerIcon->SetPosition(timerPad + std::round((badge - m_timerIcon->Width()) / 2.0f), badgeY + std::round((badge - m_timerIcon->Height()) / 2.0f));
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
    m_sun.StopAnimation(L"Offset");
    m_sun.Offset(SunAt(m_sunProgress));
    ApplyBattery(false);
}

void PeekView::OnAppear()
{
    if (!m_glance || !ShowsGlance())
    {
        return;
    }
    // A little entrance: the sun (or moon) rises from the left horizon along
    // the arc to where it is now, and the battery fills up to its charge.
    const MotionSettings& motion = *m_ui.motion;
    auto rise = m_ui.compositor.CreateVector3KeyFrameAnimation();
    const auto linear = m_ui.compositor.CreateLinearEasingFunction();
    constexpr int kSteps = 24;  // points along the curve; the motion eases out
    for (int i = 0; i <= kSteps; ++i)
    {
        const float t = static_cast<float>(i) / kSteps;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        rise.InsertKeyFrame(t, SunAt(m_sunProgress * eased), linear);
    }
    rise.Duration(std::chrono::milliseconds(900));
    rise.DelayTime(std::chrono::milliseconds(motion.contentFadeInDelayMs));
    rise.DelayBehavior(wuc::AnimationDelayBehavior::SetInitialValueBeforeDelay);  // on the horizon while it waits
    m_sun.StartAnimation(L"Offset", rise);
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

    // The sun (or the moon) moves along its arc, a little every minute.
    const SunPath path = SunPathAt(now.wHour * 60 + now.wMinute);
    m_sunProgress = path.progress;
    m_sun.StopAnimation(L"Offset");
    m_sun.Offset(SunAt(m_sunProgress));
    if (static_cast<int>(path.day) == m_skyDay)
    {
        return;
    }
    m_skyDay = path.day ? 1 : 0;
    m_sunCore->shape.FillBrush(m_ui.compositor.CreateColorBrush(path.day ? kSun : kMoon));
    m_sunGlow->shape.FillBrush(m_ui.compositor.CreateColorBrush(path.day ? kSunGlow : kMoonGlow));
    for (const auto& star : m_stars)
    {
        star->visual.Opacity(path.day ? 0.0f : 1.0f);
    }
}

Motion::float3 PeekView::SunAt(float progress) const
{
    const float angle = std::numbers::pi_v<float> * (1.0f - progress);  // left horizon (pi) over the top to the right (0)
    const float half = m_sun.Size().x / 2.0f;
    return { m_arcCenter.x + m_arcRadius * std::cos(angle) - half, m_arcCenter.y - m_arcRadius * std::sin(angle) - half, 0.0f };
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
    // How long it lasts, only on battery: on the charger the bolt says it.
    const bool detail = m_power.hasBattery && !m_power.pluggedIn && m_power.minutesLeft > 0;
    const float shown = m_power.hasBattery ? 1.0f : 0.0f;
    m_battery.Opacity(shown);
    m_batteryPercent->Visual().Opacity(shown);
    m_batteryDetail->Visual().Opacity(detail ? 0.6f : 0.0f);
    if (!m_power.hasBattery)
    {
        return;
    }

    m_batteryFill->shape.FillBrush(m_ui.compositor.CreateColorBrush(BatteryColor(BatteryToneOf(m_power))));
    m_bolt->Visual().Opacity(m_power.pluggedIn ? 1.0f : 0.0f);
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
    m_batteryDetail->SetText(detail ? PowerDescription(m_power) : L"");

    // Right-aligned: [84 %] [battery], on the time's line with how long it
    // lasts under it, or alone in the middle on the charger.
    const float right = m_width - m_ui.Px(14.0f);
    const float top = std::round((m_height - m_clock->Height() - m_date->Height()) / 2.0f);
    const float middle = detail ? top + m_clock->Height() / 2.0f : m_height / 2.0f;
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
