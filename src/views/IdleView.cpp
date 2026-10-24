#include "views/IdleView.h"

#include "platform/Settings.h"
#include "render/Glyphs.h"

void IdleView::Build()
{
    const LayoutSettings& layout = *m_ui.layout;
    ResetLayer(m_ui.EvenPx(layout.idleWidth), m_ui.Px(layout.idleHeight));

    m_mediaLayer = m_ui.compositor.CreateContainerVisual();
    m_mediaLayer.Size({ m_width, m_height });
    m_mediaLayer.Opacity(0.0f);
    m_layer.Children().InsertAtTop(m_mediaLayer);

    // Artwork on the left, inset evenly from the pill's edge.
    const float inset = m_ui.Px(6.0f);
    m_art = std::make_unique<Artwork>(m_ui, layout.idleHeight - 12.0f, 6.0f);
    m_art->SetPosition(inset + m_ui.Px(1.0f), std::round((m_height - m_art->Size()) / 2.0f));
    m_mediaLayer.Children().InsertAtTop(m_art->Visual());

    // A small, still "sound bars" mark on the right.
    m_bars = m_ui.compositor.CreateContainerVisual();
    const float barWidth = m_ui.Px(3.0f);
    const float gap = m_ui.Px(2.0f);
    const float tallest = m_ui.Px(14.0f);
    const float heights[] = { 0.45f, 0.85f, 0.6f, 1.0f };
    const float barsWidth = 4.0f * barWidth + 3.0f * gap;
    for (int i = 0; i < 4; ++i)
    {
        const float height = std::round(tallest * heights[i]);
        RoundedRect bar(m_ui, barWidth, height, barWidth / 2.0f, White(0.85f));
        bar.visual.Offset({ static_cast<float>(i) * (barWidth + gap), std::round((tallest - height) / 2.0f), 0.0f });
        m_bars.Children().InsertAtTop(bar.visual);
    }
    m_bars.Size({ barsWidth, tallest });
    m_bars.Offset({ m_width - m_ui.Px(14.0f) - barsWidth, std::round((m_height - tallest) / 2.0f), 0.0f });
    m_mediaLayer.Children().InsertAtTop(m_bars);

    auto layer = [&]
    {
        auto visual = m_ui.compositor.CreateContainerVisual();
        visual.Size({ m_width, m_height });
        visual.Opacity(0.0f);
        m_layer.Children().InsertAtTop(visual);
        return visual;
    };

    // Timer: an accent circle with the stopwatch icon, the time on the right.
    m_timerLayer = layer();
    const float badge = m_ui.EvenPx(24.0f);
    const float badgeY = std::round((m_height - badge) / 2.0f);
    m_timerBadge = std::make_unique<RoundedRect>(m_ui, badge, badge, badge / 2.0f, m_ui.accent);
    m_timerBadge->visual.Offset({ inset, badgeY, 0.0f });
    m_timerLayer.Children().InsertAtTop(m_timerBadge->visual);
    m_timerIcon = std::make_unique<Label>(m_ui, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_timerIcon->SetText(std::wstring(1, Glyph::Stopwatch));
    m_timerIcon->SetPosition(inset + std::round((badge - m_timerIcon->Width()) / 2.0f), badgeY + std::round((badge - m_timerIcon->Height()) / 2.0f));
    m_timerLayer.Children().InsertAtTop(m_timerIcon->Visual());
    m_timerTime = std::make_unique<RollingText>(m_ui, 15.0f);
    m_timerLayer.Children().InsertAtTop(m_timerTime->Visual());
    m_timerDone = std::make_unique<Label>(m_ui, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_timerDone->SetText(L"Time's up");
    m_timerDone->SetPosition(m_width - m_ui.Px(14.0f) - m_timerDone->Width(), std::round((m_height - m_timerDone->Height()) / 2.0f));
    m_timerDone->Visual().Opacity(0.0f);
    m_timerLayer.Children().InsertAtTop(m_timerDone->Visual());

    // Charging: a battery that fills up, "Charging" and the percentage.
    m_chargeLayer = layer();
    const float bodyWidth = m_ui.EvenPx(28.0f), bodyHeight = m_ui.EvenPx(14.0f);
    const float bodyX = inset + m_ui.Px(4.0f), bodyY = std::round((m_height - bodyHeight) / 2.0f);
    const float edge = std::max(1.0f, m_ui.Px(2.0f));
    m_batteryBody = std::make_unique<RoundedRect>(m_ui, bodyWidth, bodyHeight, m_ui.Px(4.0f), White(0.22f));
    m_batteryBody->visual.Offset({ bodyX, bodyY, 0.0f });
    m_batteryFill = std::make_unique<RoundedRect>(m_ui, bodyWidth - 2.0f * edge, bodyHeight - 2.0f * edge, m_ui.Px(2.5f), winrt::Windows::UI::Color{ 255, 52, 199, 89 });
    m_batteryFill->visual.Offset({ bodyX + edge, bodyY + edge, 0.0f });
    m_batteryFill->visual.CenterPoint({ 0.0f, 0.0f, 0.0f });  // fills from the left
    m_batteryFill->visual.Scale({ 0.0f, 1.0f, 1.0f });
    m_batteryTip = std::make_unique<RoundedRect>(m_ui, m_ui.Px(2.0f), std::round(bodyHeight * 0.45f), m_ui.Px(1.0f), White(0.22f));
    m_batteryTip->visual.Offset({ bodyX + bodyWidth + std::max(1.0f, m_ui.Px(1.0f)), bodyY + std::round(bodyHeight * 0.275f), 0.0f });
    m_bolt = std::make_unique<Label>(m_ui, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_bolt->SetText(std::wstring(1, Glyph::Bolt));
    m_bolt->SetPosition(bodyX + std::round((bodyWidth - m_bolt->Width()) / 2.0f), bodyY + std::round((bodyHeight - m_bolt->Height()) / 2.0f));
    m_chargingText = std::make_unique<Label>(m_ui, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_chargingText->SetText(L"Charging");
    m_chargingText->SetPosition(bodyX + bodyWidth + m_ui.Px(10.0f), std::round((m_height - m_chargingText->Height()) / 2.0f));
    m_chargePercent = std::make_unique<RollingText>(m_ui, 13.0f);
    for (const winrt::Windows::UI::Composition::Visual& visual : {
             winrt::Windows::UI::Composition::Visual(m_batteryBody->visual), winrt::Windows::UI::Composition::Visual(m_batteryFill->visual),
             winrt::Windows::UI::Composition::Visual(m_batteryTip->visual), winrt::Windows::UI::Composition::Visual(m_bolt->Visual()),
             winrt::Windows::UI::Composition::Visual(m_chargingText->Visual()), winrt::Windows::UI::Composition::Visual(m_chargePercent->Visual()) })
    {
        m_chargeLayer.Children().InsertAtTop(visual);
    }

    Apply(false);
}

void IdleView::SetTimer(const TimerState& timer, const std::wstring& text)
{
    m_timer = timer;
    m_timerText = text;
    if (m_layer)
    {
        Apply(Visible());
    }
}

void IdleView::SetCharging(bool show, int percent)
{
    const bool starting = show && !m_charging;
    m_charging = show;
    m_percent = percent;
    if (!m_layer)
    {
        return;
    }
    Apply(Visible());
    // The battery fills up to the charge, once, as it appears.
    if (starting)
    {
        const float level = std::clamp(percent / 100.0f, 0.04f, 1.0f);
        m_batteryFill->visual.StopAnimation(L"Scale");
        m_batteryFill->visual.Scale({ 0.0f, 1.0f, 1.0f });
        if (Visible())
        {
            Motion::EaseTo(m_batteryFill->visual, L"Scale", Motion::float3{ level, 1.0f, 1.0f }, 900, 250);
        }
        else
        {
            m_batteryFill->visual.Scale({ level, 1.0f, 1.0f });
        }
    }
}

void IdleView::SetMedia(const MediaState& media)
{
    m_media = media;
    if (m_layer)
    {
        Apply(Visible());  // hidden content never animates: see View::Retire
    }
}

void IdleView::Apply(bool animate)
{
    // One thing at a time: the charging moment, else a timer, else media.
    const bool charging = m_charging;
    const bool timer = !charging && m_timer.mode != TimerMode::None;
    const bool media = !charging && !timer && m_media.hasSession;
    const float bars = m_media.playing ? 1.0f : 0.35f;  // dimmed while paused
    auto fade = [&](const winrt::Windows::UI::Composition::Visual& visual, float opacity)
    {
        if (animate)
        {
            Motion::EaseTo(visual, L"Opacity", opacity, 220);
        }
        else
        {
            visual.StopAnimation(L"Opacity");
            visual.Opacity(opacity);
        }
    };
    fade(m_mediaLayer, media ? 1.0f : 0.0f);
    fade(m_bars, bars);
    fade(m_timerLayer, timer ? 1.0f : 0.0f);
    fade(m_chargeLayer, charging ? 1.0f : 0.0f);
    m_art->SetImage(m_media.artwork.get(), m_media.artwork ? m_media.artworkTrackId : 0);

    // The time, right-aligned; paused, it dims; finished, "Time's up" instead.
    m_timerTime->SetText(m_timerText, animate);
    m_timerTime->SetPosition(m_width - m_ui.Px(14.0f) - m_timerTime->Width(), std::round((m_height - m_timerTime->Height()) / 2.0f));
    fade(m_timerTime->Visual(), m_timer.finished ? 0.0f : m_timer.running ? 1.0f : 0.5f);
    fade(m_timerDone->Visual(), m_timer.finished ? 1.0f : 0.0f);

    const std::wstring percent = std::to_wstring(m_percent) + L" %";
    m_chargePercent->SetText(percent, animate);
    m_chargePercent->SetPosition(m_width - m_ui.Px(14.0f) - m_chargePercent->Width(), std::round((m_height - m_chargePercent->Height()) / 2.0f));
}
