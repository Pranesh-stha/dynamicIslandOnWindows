#include "render/Widgets.h"

#include <algorithm>
#include <chrono>

namespace wuc = winrt::Windows::UI::Composition;
using winrt::Windows::UI::Color;
using Motion::float2;
using Motion::float3;

namespace
{
    uint8_t Alpha(float opacity)
    {
        return static_cast<uint8_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    }

    // Jumps a property to a value, cancelling any animation on it.
    void SetOpacityNow(const wuc::Visual& visual, float opacity)
    {
        visual.StopAnimation(L"Opacity");
        visual.Opacity(opacity);
    }

    constexpr float kGlyphRestOpacity = 0.85f;
}

Color White(float opacity)
{
    return { Alpha(opacity), 255, 255, 255 };
}

Color Black(float opacity)
{
    return { Alpha(opacity), 0, 0, 0 };
}

// --- RoundedRect ---------------------------------------------------------------

RoundedRect::RoundedRect(const Ui& ui, float width, float height, float radius, Color fill)
{
    geometry = ui.compositor.CreateRoundedRectangleGeometry();
    shape = ui.compositor.CreateSpriteShape(geometry);
    shape.FillBrush(ui.compositor.CreateColorBrush(fill));
    visual = ui.compositor.CreateShapeVisual();
    visual.Shapes().Append(shape);
    Resize(width, height, radius);
}

void RoundedRect::Resize(float width, float height, float radius)
{
    radius = std::min(radius, std::min(width, height) / 2.0f);  // never a degenerate shape
    visual.Size({ width, height });
    geometry.Size({ width, height });
    geometry.CornerRadius({ radius, radius });
}

namespace
{
    // An "on" circle behind an icon: pops out from the middle with a little
    // bounce, and shrinks away when turned off.
    void PopCircle(const wuc::ShapeVisual& circle, bool on, bool animate, const Spring& spring)
    {
        const auto size = circle.Size();
        circle.CenterPoint({ size.x / 2.0f, size.y / 2.0f, 0.0f });
        if (!animate)
        {
            circle.StopAnimation(L"Scale");
            circle.Scale({ 1.0f, 1.0f, 1.0f });
            SetOpacityNow(circle, on ? 1.0f : 0.0f);
            return;
        }
        if (on)
        {
            circle.Scale({ 0.5f, 0.5f, 1.0f });
            Motion::SpringTo(circle, L"Scale", float3{ 1.0f, 1.0f, 1.0f }, spring);
            Motion::EaseTo(circle, L"Opacity", 1.0f, 90);
        }
        else
        {
            Motion::SpringTo(circle, L"Scale", float3{ 0.7f, 0.7f, 1.0f }, Spring{ 1.0f, spring.periodMs });
            Motion::EaseTo(circle, L"Opacity", 0.0f, 140);
        }
    }
}

// --- Label -------------------------------------------------------------------

Label::Label(const Ui& ui, float sizeDip, DWRITE_FONT_WEIGHT weight, bool icon) : m_ui(ui)
{
    m_style.sizePx = ui.scale * sizeDip;
    m_style.weight = weight;
    m_style.icon = icon;
    m_visual = ui.compositor.CreateSpriteVisual();
    if (!ui.graphics)
    {
        return;  // unit tests: controls without anything drawn
    }
    m_surface = ui.graphics->CreateSurface();
    auto brush = ui.compositor.CreateSurfaceBrush(m_surface);
    brush.Stretch(wuc::CompositionStretch::None);  // 1 surface pixel = 1 screen pixel
    brush.HorizontalAlignmentRatio(0.0f);
    brush.VerticalAlignmentRatio(0.0f);
    m_visual.Brush(brush);
}

void Label::SetText(std::wstring_view text)
{
    if (text != m_text)
    {
        m_text = text;
        Render();
    }
}

void Label::SetMaxWidth(float widthPx)
{
    if (widthPx != m_style.maxWidthPx)
    {
        m_style.maxWidthPx = widthPx;
        Render();
    }
}

void Label::SetMaxLines(int lines)
{
    if (lines != m_style.maxLines)
    {
        m_style.maxLines = lines;
        Render();
    }
}

void Label::SetPosition(float x, float y)
{
    // The text is drawn one pixel in from the surface's left edge.
    m_visual.Offset({ std::round(x) - 1.0f, std::round(y), 0.0f });
}

float Label::Height() const
{
    // An empty label still has a line height, so layouts don't jump.
    return m_size.Height > 0.0f ? m_size.Height : std::ceil(m_style.sizePx * 1.33f);
}

void Label::Render()
{
    if (m_text.empty() || !m_surface)
    {
        m_size = {};
        m_visual.Size({ 0.0f, 0.0f });
        return;
    }
    m_size = m_ui.graphics->RenderText(m_surface, m_text, m_style);
    m_visual.Size({ m_size.Width, m_size.Height });
}

// --- RollingText -------------------------------------------------------------

RollingText::RollingText(const Ui& ui, float sizeDip, DWRITE_FONT_WEIGHT weight) : m_ui(ui), m_sizeDip(sizeDip), m_weight(weight)
{
    m_root = ui.compositor.CreateContainerVisual();
    m_height = std::ceil(ui.scale * sizeDip * 1.33f);
    for (wchar_t digit = L'0'; digit <= L'9'; ++digit)
    {
        m_digitWidth = std::max(m_digitWidth, SlotWidth(digit));
    }
}

float RollingText::SlotWidth(wchar_t ch) const
{
    if (ch >= L'0' && ch <= L'9' && m_digitWidth > 0.0f)
    {
        return m_digitWidth;
    }
    if (!m_ui.graphics)
    {
        return m_ui.Px(m_sizeDip * 0.6f);  // unit tests: nothing is drawn
    }
    TextStyle style;
    style.sizePx = m_ui.scale * m_sizeDip;
    style.weight = m_weight;
    return std::max(1.0f, m_ui.graphics->MeasureText(std::wstring(1, ch), style).Width - 2.0f);
}

void RollingText::Place(Label& label, const Slot& slot, float y)
{
    label.SetPosition(std::round((slot.width - label.Width()) / 2.0f), y);
}

void RollingText::Rebuild()
{
    m_root.Children().RemoveAll();
    m_slots.clear();
    float x = 0.0f;
    for (const wchar_t ch : m_text)
    {
        Slot slot;
        slot.width = SlotWidth(ch);
        slot.box = m_ui.compositor.CreateContainerVisual();
        slot.box.Size({ slot.width, m_height });
        slot.box.Offset({ x, 0.0f, 0.0f });
        slot.box.Clip(m_ui.compositor.CreateInsetClip());  // the rolling stays inside its slot
        for (auto& label : slot.labels)
        {
            label = std::make_unique<Label>(m_ui, m_sizeDip, m_weight);
            slot.box.Children().InsertAtTop(label->Visual());
        }
        slot.labels[0]->SetText(std::wstring(1, ch));
        Place(*slot.labels[0], slot, 0.0f);
        slot.labels[1]->Visual().Opacity(0.0f);
        m_root.Children().InsertAtTop(slot.box);
        x += slot.width;
        m_slots.push_back(std::move(slot));
    }
    m_width = x;
    m_root.Size({ m_width, m_height });
}

void RollingText::SetText(std::wstring_view text, bool animate)
{
    if (text == m_text)
    {
        return;
    }
    if (text.size() != m_text.size())
    {
        m_text = text;  // e.g. 9:59 -> 10:00: laid out again, no roll
        Rebuild();
        return;
    }
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == m_text[i])
        {
            continue;
        }
        Slot& slot = m_slots[i];
        Label& out = *slot.labels[slot.shown];
        Label& in = *slot.labels[1 - slot.shown];
        in.SetText(std::wstring(1, text[i]));
        if (animate)
        {
            // The new character comes up from below as the old one leaves upwards.
            Place(in, slot, m_height);
            in.Visual().StopAnimation(L"Opacity");
            in.Visual().Opacity(0.0f);
            Motion::EaseTo(in.Visual(), L"Offset.Y", 0.0f, 280);
            Motion::EaseTo(in.Visual(), L"Opacity", 1.0f, 180);
            Motion::EaseTo(out.Visual(), L"Offset.Y", -m_height * 0.7f, 280);
            Motion::EaseTo(out.Visual(), L"Opacity", 0.0f, 160);
        }
        else
        {
            in.Visual().StopAnimation(L"Offset.Y");
            out.Visual().StopAnimation(L"Offset.Y");
            Place(in, slot, 0.0f);
            in.Visual().StopAnimation(L"Opacity");
            in.Visual().Opacity(1.0f);
            out.Visual().StopAnimation(L"Opacity");
            out.Visual().Opacity(0.0f);
        }
        slot.shown = 1 - slot.shown;
    }
    m_text = text;
}

void RollingText::SetPosition(float x, float y)
{
    m_root.Offset({ std::round(x), std::round(y), 0.0f });
}

// --- Ripple ------------------------------------------------------------------

Ripple::Ripple(const Ui& ui) : m_ui(ui)
{
    m_circle = ui.compositor.CreateEllipseGeometry();
    m_circle.Radius({ 0.0f, 0.0f });
    auto shape = ui.compositor.CreateSpriteShape(m_circle);
    shape.FillBrush(ui.compositor.CreateColorBrush(White(0.16f)));
    m_visual = ui.compositor.CreateShapeVisual();
    m_visual.Shapes().Append(shape);
    m_bounds = ui.compositor.CreateRoundedRectangleGeometry();
    m_visual.Clip(ui.compositor.CreateGeometricClip(m_bounds));
    m_visual.Opacity(0.0f);
}

void Ripple::Resize(float width, float height, float radius)
{
    m_width = width;
    m_height = height;
    m_visual.Size({ width, height });
    m_bounds.Size({ width, height });
    radius = std::min(radius, std::min(width, height) / 2.0f);
    m_bounds.CornerRadius({ radius, radius });
}

void Ripple::Start(float x, float y)
{
    // Big enough to reach the far corner from where you pressed.
    const float dx = std::max(x, m_width - x), dy = std::max(y, m_height - y);
    const float full = std::sqrt(dx * dx + dy * dy);
    m_circle.Center({ x, y });
    m_circle.StopAnimation(L"Radius");
    m_circle.Radius({ 0.0f, 0.0f });
    Motion::EaseTo(m_circle, L"Radius", float2{ full, full }, 420);
    m_visual.StopAnimation(L"Opacity");
    m_visual.Opacity(1.0f);
    Motion::EaseTo(m_visual, L"Opacity", 0.0f, 520, 160);
}

// --- Control -----------------------------------------------------------------

Control::Control(const Ui& ui) : m_ui(ui)
{
    m_root = ui.compositor.CreateContainerVisual();
}

void Control::SetBounds(float x, float y, float width, float height)
{
    m_x = std::round(x);
    m_y = std::round(y);
    m_width = width;
    m_height = height;
    m_root.Offset({ m_x, m_y, 0.0f });
    m_root.Size({ width, height });
    m_root.CenterPoint({ width / 2.0f, height / 2.0f, 0.0f });  // presses shrink towards the middle
    Layout();
}

bool Control::Contains(float x, float y) const
{
    return x >= m_x && x < m_x + m_width && y >= m_y && y < m_y + m_height;
}

void Control::SetEnabled(bool enabled)
{
    if (enabled != m_enabled)
    {
        m_enabled = enabled;
        Motion::EaseTo(m_root, L"Opacity", enabled ? 1.0f : 0.35f, m_ui.motion->hoverFadeMs * 2);
    }
}

// --- ControlHost -------------------------------------------------------------

void ControlHost::Clear()
{
    m_controls.clear();
    m_hovered = nullptr;
    m_pressed = nullptr;
}

Control* ControlHost::Hit(float x, float y) const
{
    for (auto it = m_controls.rbegin(); it != m_controls.rend(); ++it)
    {
        if ((*it)->Enabled() && (*it)->Interactive() && (*it)->Contains(x, y))
        {
            return *it;
        }
    }
    return nullptr;
}

void ControlHost::SetHovered(Control* control)
{
    if (control == m_hovered)
    {
        return;
    }
    if (m_hovered)
    {
        m_hovered->OnHoverChanged(false);
    }
    m_hovered = control;
    if (control)
    {
        control->OnHoverChanged(true);
    }
}

void ControlHost::PointerMove(float x, float y)
{
    if (m_pressed)
    {
        m_pressed->OnPointerDrag(x - m_pressed->X(), y - m_pressed->Y());
        const bool inside = m_pressed->Contains(x, y);
        if (!m_pressed->KeepsPressOutside() && inside != m_pressedInside)
        {
            // Dragging off a button releases its pressed look (and cancels the click).
            m_pressedInside = inside;
            m_pressed->OnPressChanged(inside);
        }
        return;
    }
    Control* hit = Hit(x, y);
    SetHovered(hit);
    if (hit)
    {
        hit->OnPointerMove(x - hit->X(), y - hit->Y());
    }
}

bool ControlHost::PointerDown(float x, float y)
{
    Control* hit = Hit(x, y);
    if (!hit)
    {
        return false;
    }
    SetHovered(hit);
    m_pressed = hit;
    m_pressedInside = true;
    hit->OnPressChanged(true);
    hit->OnPointerDown(x - hit->X(), y - hit->Y());
    return true;
}

void ControlHost::PointerUp(float x, float y)
{
    if (!m_pressed)
    {
        return;
    }
    Control* control = m_pressed;
    m_pressed = nullptr;
    const bool inside = control->Contains(x, y);
    if (m_pressedInside || control->KeepsPressOutside())
    {
        control->OnPressChanged(false);
    }
    PointerMove(x, y);  // update hover before the action, which may change the view
    control->OnPointerUp(inside);
}

void ControlHost::PointerLeave()
{
    if (!m_pressed)
    {
        SetHovered(nullptr);
    }
}

void ControlHost::CancelPress()
{
    if (m_pressed)
    {
        Control* control = m_pressed;
        m_pressed = nullptr;
        control->OnPressChanged(false);
        control->OnPointerUp(false);
    }
    SetHovered(nullptr);
}

bool ControlHost::RightClick(float x, float y)
{
    if (Control* hit = Hit(x, y))
    {
        hit->OnRightClick();
        return true;
    }
    return false;
}

bool ControlHost::Wheel(float x, float y, float notches)
{
    Control* hit = Hit(x, y);
    return hit && hit->OnWheel(notches);
}

// --- IconButton --------------------------------------------------------------

IconButton::IconButton(const Ui& ui, wchar_t glyph, float diameterDip, float glyphDip) : Control(ui), m_glyph(glyph)
{
    const float diameter = ui.EvenPx(diameterDip);
    m_highlight = RoundedRect(ui, diameter, diameter, diameter / 2.0f, White(0.16f));
    m_highlight.visual.Opacity(0.0f);
    m_root.Children().InsertAtTop(m_highlight.visual);

    for (auto& label : m_glyphs)
    {
        label = std::make_unique<Label>(ui, glyphDip, DWRITE_FONT_WEIGHT_NORMAL, true);
        m_root.Children().InsertAtTop(label->Visual());
    }
    m_glyphs[0]->SetText(std::wstring(1, glyph));
    m_glyphs[0]->Visual().Opacity(kGlyphRestOpacity);
    m_glyphs[1]->Visual().Opacity(0.0f);
    SetBounds(0.0f, 0.0f, diameter, diameter);
}

void IconButton::Layout()
{
    m_highlight.Resize(m_width, m_height, std::min(m_width, m_height) / 2.0f);
    for (auto& label : m_glyphs)
    {
        CenterGlyph(*label);
    }
}

void IconButton::CenterGlyph(Label& glyph)
{
    glyph.SetPosition(std::round((m_width - glyph.Width()) / 2.0f), std::round((m_height - glyph.Height()) / 2.0f));
    const auto size = glyph.Visual().Size();
    glyph.Visual().CenterPoint({ size.x / 2.0f, size.y / 2.0f, 0.0f });  // turn and shrink around its middle
}

float IconButton::RestingGlyphOpacity() const
{
    return m_hovered ? 1.0f : kGlyphRestOpacity;
}

void IconButton::OnHoverChanged(bool hovered)
{
    m_hovered = hovered;
    const int fade = m_ui.motion->hoverFadeMs;
    Motion::EaseTo(m_highlight.visual, L"Opacity", m_pressed ? 1.0f : hovered ? 0.7f : 0.0f, fade);
    Motion::EaseTo(m_glyphs[m_front]->Visual(), L"Opacity", RestingGlyphOpacity(), fade);
}

void IconButton::OnPressChanged(bool pressed)
{
    m_pressed = pressed;
    Motion::SpringTo(m_root, L"Scale", pressed ? float3{ 0.93f, 0.93f, 1.0f } : float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
    Motion::EaseTo(m_highlight.visual, L"Opacity", pressed ? 1.0f : m_hovered ? 0.7f : 0.0f, m_ui.motion->hoverFadeMs / 2);
}

void IconButton::OnClick()
{
    if (onClick)
    {
        onClick();
    }
}

void IconButton::SetGlyph(wchar_t glyph, bool morph, bool animate)
{
    if (glyph == m_glyph)
    {
        return;
    }
    m_glyph = glyph;
    const int outgoing = m_front;
    const int incoming = 1 - m_front;
    m_front = incoming;

    Label& next = *m_glyphs[incoming];
    next.SetText(std::wstring(1, glyph));
    CenterGlyph(next);
    const auto in = next.Visual();
    const auto out = m_glyphs[outgoing]->Visual();
    const Spring& spring = m_ui.motion->toggle;

    if (!animate)
    {
        for (const auto& visual : { in, out })
        {
            visual.StopAnimation(L"Opacity");
            visual.StopAnimation(L"Scale");
            visual.StopAnimation(L"RotationAngleInDegrees");
            visual.Scale({ 1.0f, 1.0f, 1.0f });
            visual.RotationAngleInDegrees(0.0f);
        }
        in.Opacity(RestingGlyphOpacity());
        out.Opacity(0.0f);
        m_hiddenAt[outgoing] = 0;
        return;
    }

    // If the incoming icon finished leaving a while ago, start it turned back
    // and small. If it's still on its way out (fast double clicks), it simply
    // turns around from wherever it is, so nothing ever jumps.
    if (morph && GetTickCount64() - m_hiddenAt[incoming] > 300)
    {
        in.StopAnimation(L"Scale");
        in.StopAnimation(L"RotationAngleInDegrees");
        in.Scale({ 0.5f, 0.5f, 1.0f });
        in.RotationAngleInDegrees(-35.0f);
    }
    Motion::EaseTo(out, L"Opacity", 0.0f, 110);
    Motion::EaseTo(in, L"Opacity", RestingGlyphOpacity(), 160, 40);
    if (morph)
    {
        Motion::SpringTo(out, L"Scale", float3{ 0.5f, 0.5f, 1.0f }, spring);
        Motion::SpringTo(out, L"RotationAngleInDegrees", 35.0f, spring);
        Motion::SpringTo(in, L"Scale", float3{ 1.0f, 1.0f, 1.0f }, spring);
        Motion::SpringTo(in, L"RotationAngleInDegrees", 0.0f, spring);
    }
    m_hiddenAt[outgoing] = GetTickCount64();
}

// --- ToggleSwitch ------------------------------------------------------------

ToggleSwitch::ToggleSwitch(const Ui& ui) : m_ui(ui)
{
    m_width = ui.EvenPx(40.0f);
    m_height = ui.EvenPx(22.0f);
    m_pad = ui.Px(3.0f);
    m_knobSize = m_height - 2.0f * m_pad;
    m_knobStretched = m_knobSize + ui.Px(5.0f);

    m_root = ui.compositor.CreateContainerVisual();
    m_root.Size({ m_width, m_height });
    const float radius = m_height / 2.0f;
    m_off = RoundedRect(ui, m_width, m_height, radius, White(0.22f));
    m_on = RoundedRect(ui, m_width, m_height, radius, ui.accent);
    m_hover = RoundedRect(ui, m_width, m_height, radius, White(0.12f));
    m_on.visual.Opacity(0.0f);
    m_hover.visual.Opacity(0.0f);
    m_root.Children().InsertAtTop(m_off.visual);
    m_root.Children().InsertAtTop(m_on.visual);
    m_root.Children().InsertAtTop(m_hover.visual);

    m_knob = ui.compositor.CreateRoundedRectangleGeometry();
    m_knob.CornerRadius({ m_knobSize / 2.0f, m_knobSize / 2.0f });
    auto knobShape = ui.compositor.CreateSpriteShape(m_knob);
    knobShape.FillBrush(ui.compositor.CreateColorBrush(White(1.0f)));
    m_knobLayer = ui.compositor.CreateShapeVisual();
    m_knobLayer.Size({ m_width, m_height });
    m_knobLayer.Shapes().Append(knobShape);
    m_root.Children().InsertAtTop(m_knobLayer);
    PlaceKnob(false);
}

void ToggleSwitch::SetOn(bool on, bool animate)
{
    m_isOn = on;
    if (animate)
    {
        Motion::EaseTo(m_on.visual, L"Opacity", on ? 1.0f : 0.0f, 180);
    }
    else
    {
        SetOpacityNow(m_on.visual, on ? 1.0f : 0.0f);
    }
    PlaceKnob(animate);
}

void ToggleSwitch::SetHovered(bool hovered)
{
    Motion::EaseTo(m_hover.visual, L"Opacity", hovered ? 1.0f : 0.0f, m_ui.motion->hoverFadeMs);
}

void ToggleSwitch::SetPressed(bool pressed)
{
    m_pressed = pressed;
    PlaceKnob(true);
}

void ToggleSwitch::SetBusy(bool busy)
{
    if (busy == m_busy)
    {
        return;
    }
    m_busy = busy;
    if (busy)
    {
        Motion::Pulse(m_knobLayer, L"Opacity", 1.0f, 0.45f, 900);
    }
    else
    {
        m_knobLayer.StopAnimation(L"Opacity");
        Motion::EaseTo(m_knobLayer, L"Opacity", 1.0f, 150);
    }
}

void ToggleSwitch::PlaceKnob(bool animate)
{
    // While held, the knob stretches towards the side it would slide to.
    const float width = m_pressed ? m_knobStretched : m_knobSize;
    const float x = m_isOn ? m_width - m_pad - width : m_pad;
    if (animate)
    {
        Motion::SpringTo(m_knob, L"Offset", float2{ x, m_pad }, m_ui.motion->toggle);
        Motion::SpringTo(m_knob, L"Size", float2{ width, m_knobSize }, m_ui.motion->toggle);
    }
    else
    {
        m_knob.StopAnimation(L"Offset");
        m_knob.StopAnimation(L"Size");
        m_knob.Offset({ x, m_pad });
        m_knob.Size({ width, m_knobSize });
    }
}

// --- ToggleTile --------------------------------------------------------------

ToggleTile::ToggleTile(const Ui& ui, wchar_t glyph, std::wstring_view title)
    : Control(ui),
      m_ripple(ui),
      m_icon(ui, 15.0f, DWRITE_FONT_WEIGHT_NORMAL, true),
      m_title(ui, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD),
      m_subtitle(ui, 11.5f),
      m_switch(ui)
{
    m_background = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.08f));
    m_selectedLayer = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.12f));
    m_selectedLayer.visual.Opacity(0.0f);
    m_hoverLayer = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.06f));
    m_hoverLayer.visual.Opacity(0.0f);
    const float iconSize = ui.EvenPx(32.0f);
    m_iconOff = RoundedRect(ui, iconSize, iconSize, iconSize / 2.0f, White(0.14f));
    m_iconOn = RoundedRect(ui, iconSize, iconSize, iconSize / 2.0f, ui.accent);
    m_iconOn.visual.Opacity(0.0f);

    m_icon.SetText(std::wstring(1, glyph));
    m_title.SetText(title);
    m_subtitle.Visual().Opacity(0.6f);

    for (const wuc::Visual& visual : { wuc::Visual(m_background.visual), wuc::Visual(m_selectedLayer.visual),
                                       wuc::Visual(m_hoverLayer.visual), wuc::Visual(m_ripple.Visual()), wuc::Visual(m_iconOff.visual),
                                       wuc::Visual(m_iconOn.visual), wuc::Visual(m_icon.Visual()),
                                       wuc::Visual(m_title.Visual()), wuc::Visual(m_subtitle.Visual()),
                                       wuc::Visual(m_switch.Visual()) })
    {
        m_root.Children().InsertAtTop(visual);
    }
}

void ToggleTile::Layout()
{
    const float radius = m_ui.Px(14.0f);
    m_background.Resize(m_width, m_height, radius);
    m_selectedLayer.Resize(m_width, m_height, radius);
    m_hoverLayer.Resize(m_width, m_height, radius);
    m_ripple.Resize(m_width, m_height, radius);

    const float pad = m_ui.Px(12.0f);
    const float iconSize = m_iconOff.visual.Size().x;
    const float iconY = std::round((m_height - iconSize) / 2.0f);
    m_iconOff.visual.Offset({ pad, iconY, 0.0f });
    m_iconOn.visual.Offset({ pad, iconY, 0.0f });
    m_icon.SetPosition(pad + std::round((iconSize - m_icon.Width()) / 2.0f), iconY + std::round((iconSize - m_icon.Height()) / 2.0f));

    m_switchX = m_width - pad - m_switch.Width();
    m_switch.Visual().Offset({ m_switchX, std::round((m_height - m_switch.Height()) / 2.0f), 0.0f });

    const float textX = pad + iconSize + m_ui.Px(10.0f);
    const float textEnd = m_compact ? m_width - pad : m_switchX - m_ui.Px(8.0f);
    const float textWidth = std::max(0.0f, textEnd - textX);
    m_title.SetMaxWidth(textWidth);
    m_subtitle.SetMaxWidth(textWidth);
    const float textTop = std::round((m_height - m_title.Height() - m_subtitle.Height()) / 2.0f);
    m_title.SetPosition(textX, textTop);
    m_subtitle.SetPosition(textX, textTop + m_title.Height());
}

void ToggleTile::SetCompact(bool compact)
{
    m_compact = compact;
    // The hidden switch never animates (springs on unseen visuals crash DWM, PLAN.md D11).
    m_switch.Visual().IsVisible(!compact);
}

void ToggleTile::SetBusy(bool busy)
{
    if (!m_compact)
    {
        m_switch.SetBusy(busy);
        return;
    }
    // Compact: the icon pulses instead of the switch's knob.
    if (busy == m_busy)
    {
        return;
    }
    m_busy = busy;
    if (busy)
    {
        Motion::Pulse(m_icon.Visual(), L"Opacity", 1.0f, 0.35f, 900);
    }
    else
    {
        m_icon.Visual().StopAnimation(L"Opacity");
        m_icon.Visual().Opacity(1.0f);
    }
}

void ToggleTile::SetOn(bool on, bool animate)
{
    const bool changed = on != m_isOn;
    m_isOn = on;
    m_switch.SetOn(on, animate && !m_compact);
    PopCircle(m_iconOn.visual, on, animate && changed, m_ui.motion->toggle);
}

void ToggleTile::SetSubtitle(std::wstring_view text)
{
    m_subtitle.SetText(text);
}

void ToggleTile::SetSelected(bool selected, bool animate)
{
    if (animate)
    {
        Motion::EaseTo(m_selectedLayer.visual, L"Opacity", selected ? 1.0f : 0.0f, 180);
    }
    else
    {
        SetOpacityNow(m_selectedLayer.visual, selected ? 1.0f : 0.0f);
    }
}

void ToggleTile::OnHoverChanged(bool hovered)
{
    Motion::EaseTo(m_hoverLayer.visual, L"Opacity", hovered ? 1.0f : 0.0f, m_ui.motion->hoverFadeMs);
    if (!m_compact)
    {
        m_switch.SetHovered(hovered);
    }
}

void ToggleTile::OnPointerDown(float x, float y)
{
    m_ripple.Start(x, y);
    if (m_compact)
    {
        // The round icon and a little room around it is the on/off button.
        m_pressOnSwitch = !onSelect || x <= m_ui.Px(12.0f) + m_iconOff.visual.Size().x + m_ui.Px(4.0f);
        return;
    }
    // The switch and a little room around it count as "the switch".
    m_pressOnSwitch = !onSelect || x >= m_switchX - m_ui.Px(8.0f);
    m_switch.SetPressed(m_pressOnSwitch);  // the knob stretches only when you hold the switch
}

void ToggleTile::OnPressChanged(bool pressed)
{
    Motion::SpringTo(m_root, L"Scale", pressed ? float3{ 0.97f, 0.97f, 1.0f } : float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
    if (!pressed && !m_compact)
    {
        m_switch.SetPressed(false);
    }
}

void ToggleTile::OnClick()
{
    if (onSelect && !m_pressOnSwitch)
    {
        onSelect();
        return;
    }
    SetOn(!m_isOn);
    if (onToggle)
    {
        onToggle(m_isOn);
    }
}

// --- ToggleChip --------------------------------------------------------------

ToggleChip::ToggleChip(const Ui& ui, wchar_t glyph, std::wstring_view label)
    : Control(ui),
      m_ripple(ui),
      m_icon(ui, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, true),
      m_label(ui, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD)
{
    m_background = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.10f));
    m_onLayer = RoundedRect(ui, 1.0f, 1.0f, 0.0f, ui.accent);
    // Seen through a circle that grows out of the icon when it turns on.
    m_bloom = ui.compositor.CreateEllipseGeometry();
    m_bloom.Radius({ 0.0f, 0.0f });
    m_onLayer.visual.Clip(ui.compositor.CreateGeometricClip(m_bloom));
    m_hoverLayer = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.08f));
    m_hoverLayer.visual.Opacity(0.0f);
    m_icon.SetText(glyph ? std::wstring(1, glyph) : std::wstring());
    m_label.SetText(label);
    for (const wuc::Visual& visual : { wuc::Visual(m_background.visual), wuc::Visual(m_onLayer.visual), wuc::Visual(m_hoverLayer.visual),
                                       wuc::Visual(m_ripple.Visual()), wuc::Visual(m_icon.Visual()), wuc::Visual(m_label.Visual()) })
    {
        m_root.Children().InsertAtTop(visual);
    }
}

float ToggleChip::NaturalWidth() const
{
    // Icon and words; or just one of them, evenly padded.
    if (m_label.Text().empty())
    {
        return std::round(m_icon.Width() + 2.0f * m_ui.Px(10.0f));
    }
    if (m_icon.Text().empty())
    {
        return std::round(m_label.Width() + 2.0f * m_ui.Px(12.0f));
    }
    return std::round(m_ui.Px(12.0f) + m_icon.Width() + m_ui.Px(7.0f) + m_label.Width() + m_ui.Px(14.0f));
}

void ToggleChip::SetLabel(std::wstring_view label)
{
    m_label.SetText(label);
    Layout();
}

void ToggleChip::Layout()
{
    for (RoundedRect* rect : { &m_background, &m_onLayer, &m_hoverLayer })
    {
        rect->Resize(m_width, m_height, m_height / 2.0f);
    }
    m_ripple.Resize(m_width, m_height, m_height / 2.0f);
    // Centred when there's only one of the two.
    const float iconX = m_label.Text().empty() ? std::round((m_width - m_icon.Width()) / 2.0f) : m_ui.Px(12.0f);
    m_icon.SetPosition(iconX, std::round((m_height - m_icon.Height()) / 2.0f));
    const float labelX = m_icon.Text().empty() ? std::round((m_width - m_label.Width()) / 2.0f) : iconX + m_icon.Width() + m_ui.Px(7.0f);
    m_label.SetPosition(labelX, std::round((m_height - m_label.Height()) / 2.0f));
    m_bloom.Center({ iconX + m_icon.Width() / 2.0f, m_height / 2.0f });
    if (m_on)
    {
        m_bloom.StopAnimation(L"Radius");
        const float full = FullBloom();
        m_bloom.Radius({ full, full });
    }
}

float ToggleChip::FullBloom() const
{
    // Far enough to reach the chip's farthest corner from the icon.
    const float x = std::max(m_bloom.Center().x, m_width - m_bloom.Center().x);
    return std::sqrt(x * x + m_height * m_height) + 1.0f;
}

void ToggleChip::SetOn(bool on, bool animate)
{
    if (on == m_on)
    {
        return;
    }
    m_on = on;
    const float radius = on ? FullBloom() : 0.0f;
    if (animate)
    {
        // Spreads out with a little bounce; draws back in without one (a
        // circle can't shrink past nothing).
        Motion::SpringTo(m_bloom, L"Radius", float2{ radius, radius },
                         on ? m_ui.motion->toggle : Spring{ 1.0f, m_ui.motion->toggle.periodMs });
    }
    else
    {
        m_bloom.StopAnimation(L"Radius");
        m_bloom.Radius({ radius, radius });
    }
}

void ToggleChip::SetGlyph(wchar_t glyph)
{
    m_icon.SetText(glyph ? std::wstring(1, glyph) : std::wstring());
}

void ToggleChip::SetOnColor(winrt::Windows::UI::Color color)
{
    m_onLayer.shape.FillBrush(m_ui.compositor.CreateColorBrush(color));
}

void ToggleChip::SetBusy(bool busy)
{
    if (busy == m_busy)
    {
        return;
    }
    m_busy = busy;
    if (busy)
    {
        Motion::Pulse(m_icon.Visual(), L"Opacity", 1.0f, 0.35f, 900);
    }
    else
    {
        m_icon.Visual().StopAnimation(L"Opacity");
        m_icon.Visual().Opacity(1.0f);
    }
}

void ToggleChip::OnHoverChanged(bool hovered)
{
    Motion::EaseTo(m_hoverLayer.visual, L"Opacity", hovered ? 1.0f : 0.0f, m_ui.motion->hoverFadeMs);
}

void ToggleChip::OnPressChanged(bool pressed)
{
    Motion::SpringTo(m_root, L"Scale", pressed ? float3{ 0.94f, 0.94f, 1.0f } : float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
}

void ToggleChip::OnPointerDown(float x, float y)
{
    m_ripple.Start(x, y);
}

void ToggleChip::OnClick()
{
    if (onClick)
    {
        onClick();
    }
}

// --- ListRow -----------------------------------------------------------------

ListRow::ListRow(const Ui& ui)
    : Control(ui),
      m_ripple(ui),
      m_icon(ui, 14.0f, DWRITE_FONT_WEIGHT_NORMAL, true),
      m_title(ui, 13.0f),
      m_detail(ui, 11.0f),
      m_trailing(ui, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, true)
{
    m_highlight = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.08f));
    m_highlight.visual.Opacity(0.0f);
    const float iconSize = ui.EvenPx(28.0f);
    m_iconOff = RoundedRect(ui, iconSize, iconSize, iconSize / 2.0f, White(0.12f));
    m_iconOn = RoundedRect(ui, iconSize, iconSize, iconSize / 2.0f, ui.accent);
    m_iconOn.visual.Opacity(0.0f);
    m_detail.Visual().Opacity(0.55f);
    m_trailing.Visual().Opacity(0.5f);
    for (const wuc::Visual& visual : { wuc::Visual(m_highlight.visual), wuc::Visual(m_ripple.Visual()), wuc::Visual(m_iconOff.visual),
                                       wuc::Visual(m_iconOn.visual), wuc::Visual(m_icon.Visual()),
                                       wuc::Visual(m_title.Visual()), wuc::Visual(m_detail.Visual()),
                                       wuc::Visual(m_trailing.Visual()) })
    {
        m_root.Children().InsertAtTop(visual);
    }
}

void ListRow::Layout()
{
    m_highlight.Resize(m_width, m_height, m_ui.Px(10.0f));
    m_ripple.Resize(m_width, m_height, m_ui.Px(10.0f));
    Arrange();
}

void ListRow::Arrange()
{
    const float pad = m_ui.Px(5.0f);
    const float iconSize = m_iconOff.visual.Size().x;
    const float iconY = std::round((m_height - iconSize) / 2.0f);
    m_iconOff.visual.Offset({ pad, iconY, 0.0f });
    m_iconOn.visual.Offset({ pad, iconY, 0.0f });
    m_icon.SetPosition(pad + std::round((iconSize - m_icon.Width()) / 2.0f), iconY + std::round((iconSize - m_icon.Height()) / 2.0f));

    const float trailingRoom = m_trailing.Text().empty() ? 0.0f : m_trailing.Width() + m_ui.Px(14.0f);
    m_trailing.SetPosition(m_width - m_ui.Px(10.0f) - m_trailing.Width(), std::round((m_height - m_trailing.Height()) / 2.0f));
    const float textX = pad + iconSize + m_ui.Px(10.0f);
    const float textWidth = std::max(1.0f, m_width - textX - m_ui.Px(8.0f) - trailingRoom);
    m_title.SetMaxWidth(textWidth);
    m_detail.SetMaxWidth(textWidth);
    const float detailHeight = m_detail.Text().empty() ? 0.0f : m_detail.Height();
    const float textTop = std::round((m_height - m_title.Height() - detailHeight) / 2.0f);
    m_title.SetPosition(textX, textTop);
    m_detail.SetPosition(textX, textTop + m_title.Height());
}

void ListRow::SetContent(wchar_t glyph, std::wstring_view title, std::wstring_view detail, wchar_t trailing, bool active, bool animate)
{
    m_icon.SetText(glyph ? std::wstring(1, glyph) : std::wstring());
    m_title.SetText(title);
    m_detail.SetText(detail);
    m_trailing.SetText(trailing ? std::wstring(1, trailing) : std::wstring());
    Arrange();
    if (active != m_active)
    {
        m_active = active;
        PopCircle(m_iconOn.visual, active, animate, m_ui.motion->toggle);
    }
}

void ListRow::SetBusy(bool busy)
{
    if (busy == m_busy)
    {
        return;
    }
    m_busy = busy;
    if (busy)
    {
        Motion::Pulse(m_icon.Visual(), L"Opacity", 1.0f, 0.35f, 900);
    }
    else
    {
        m_icon.Visual().StopAnimation(L"Opacity");
        m_icon.Visual().Opacity(1.0f);
    }
}

void ListRow::SetShown(bool shown, bool animate)
{
    SetInteractive(shown);
    if (animate)
    {
        Motion::EaseTo(m_root, L"Opacity", shown ? 1.0f : 0.0f, 160);
    }
    else
    {
        SetOpacityNow(m_root, shown ? 1.0f : 0.0f);
    }
    if (!shown)
    {
        SetBusy(false);
    }
}

void ListRow::OnHoverChanged(bool hovered)
{
    if (m_spotlight)
    {
        if (hovered)
        {
            m_spotlight->MoveTo(m_x, m_y, m_width, m_height);
        }
        else
        {
            m_spotlight->Leave();
        }
        return;
    }
    Motion::EaseTo(m_highlight.visual, L"Opacity", hovered ? 1.0f : 0.0f, m_ui.motion->hoverFadeMs);
}

// --- Spotlight ---------------------------------------------------------------

Spotlight::Spotlight(const Ui& ui) : m_ui(ui)
{
    m_rect = RoundedRect(ui, 1.0f, 1.0f, 0.0f, White(0.08f));
    m_rect.visual.Opacity(0.0f);
}

void Spotlight::MoveTo(float x, float y, float width, float height)
{
    // Coming straight from another row (it was just left): glide there.
    // Otherwise appear in place.
    const bool glide = m_shown || GetTickCount64() - m_leftAt < 150;
    const float radius = m_ui.Px(10.0f);
    m_rect.visual.Size({ width, height });
    m_rect.geometry.CornerRadius({ radius, radius });
    if (glide)
    {
        Motion::SpringTo(m_rect.visual, L"Offset", float3{ x, y, 0.0f }, m_ui.motion->toggle);
        Motion::SpringTo(m_rect.geometry, L"Size", float2{ width, height }, m_ui.motion->toggle);
    }
    else
    {
        m_rect.visual.StopAnimation(L"Offset");
        m_rect.geometry.StopAnimation(L"Size");
        m_rect.visual.Offset({ x, y, 0.0f });
        m_rect.geometry.Size({ width, height });
    }
    Motion::EaseTo(m_rect.visual, L"Opacity", 1.0f, m_ui.motion->hoverFadeMs);
    m_shown = true;
}

void Spotlight::Leave()
{
    if (!m_shown)
    {
        return;
    }
    m_shown = false;
    m_leftAt = GetTickCount64();
    // A moment's wait: moving onto the next row comes right after leaving this one.
    Motion::EaseTo(m_rect.visual, L"Opacity", 0.0f, m_ui.motion->hoverFadeMs, 60);
}

void Spotlight::Hide()
{
    m_shown = false;
    m_leftAt = 0;
    m_rect.visual.StopAnimation(L"Opacity");
    m_rect.visual.StopAnimation(L"Offset");
    m_rect.geometry.StopAnimation(L"Size");
    m_rect.visual.Opacity(0.0f);
}

void ListRow::OnPressChanged(bool pressed)
{
    Motion::SpringTo(m_root, L"Scale", pressed ? float3{ 0.98f, 0.98f, 1.0f } : float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
}

void ListRow::OnPointerDown(float x, float y)
{
    m_ripple.Start(x, y);
}

void ListRow::OnClick()
{
    if (onClick)
    {
        onClick();
    }
}

// --- Segmented ---------------------------------------------------------------

Segmented::Segmented(const Ui& ui, std::vector<std::wstring> options) : Control(ui), m_options(std::move(options))
{
    m_background = RoundedRect(ui, 1.0f, 1.0f, 1.0f, White(0.07f));
    m_root.Children().InsertAtTop(m_background.visual);
    for (size_t i = 0; i < m_options.size(); ++i)
    {
        m_highlights.emplace_back(ui, 1.0f, 1.0f, 1.0f, White(0.06f));
        m_highlights.back().visual.Opacity(0.0f);
        m_root.Children().InsertAtTop(m_highlights.back().visual);
    }

    m_selection = ui.compositor.CreateRoundedRectangleGeometry();
    auto selectionShape = ui.compositor.CreateSpriteShape(m_selection);
    selectionShape.FillBrush(ui.compositor.CreateColorBrush(White(0.2f)));
    m_selectionLayer = ui.compositor.CreateShapeVisual();
    m_selectionLayer.Shapes().Append(selectionShape);
    m_root.Children().InsertAtTop(m_selectionLayer);

    for (const auto& option : m_options)
    {
        auto label = std::make_unique<Label>(ui, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        label->SetText(option);
        m_root.Children().InsertAtTop(label->Visual());
        m_labels.push_back(std::move(label));
    }
}

void Segmented::Layout()
{
    const float pad = m_ui.Px(3.0f);
    const float inner = m_height - 2.0f * pad;
    const float optionWidth = std::floor((m_width - 2.0f * pad) / static_cast<float>(m_options.size()));
    m_background.Resize(m_width, m_height, m_height / 2.0f);
    m_selectionLayer.Size({ m_width, m_height });
    m_selection.CornerRadius({ inner / 2.0f, inner / 2.0f });
    for (size_t i = 0; i < m_options.size(); ++i)
    {
        const float x = pad + optionWidth * static_cast<float>(i);
        m_highlights[i].Resize(optionWidth, inner, inner / 2.0f);
        m_highlights[i].visual.Offset({ x, pad, 0.0f });
        Label& label = *m_labels[i];
        label.SetMaxWidth(optionWidth - m_ui.Px(8.0f));
        label.SetPosition(x + std::round((optionWidth - label.Width()) / 2.0f), std::round((m_height - label.Height()) / 2.0f));
        const auto size = label.Visual().Size();
        label.Visual().CenterPoint({ size.x / 2.0f, size.y / 2.0f, 0.0f });
    }
    PlaceSelection(false);
}

int Segmented::OptionAt(float x) const
{
    const float pad = m_ui.Px(3.0f);
    const float optionWidth = (m_width - 2.0f * pad) / static_cast<float>(m_options.size());
    return std::clamp(static_cast<int>((x - pad) / optionWidth), 0, static_cast<int>(m_options.size()) - 1);
}

void Segmented::PlaceSelection(bool animate)
{
    const float pad = m_ui.Px(3.0f);
    const float optionWidth = std::floor((m_width - 2.0f * pad) / static_cast<float>(m_options.size()));
    const float2 offset{ pad + optionWidth * static_cast<float>(m_selected), pad };
    const float2 size{ optionWidth, m_height - 2.0f * pad };
    if (animate)
    {
        Motion::SpringTo(m_selection, L"Offset", offset, m_ui.motion->toggle);
        Motion::SpringTo(m_selection, L"Size", size, m_ui.motion->toggle);
    }
    else
    {
        m_selection.StopAnimation(L"Offset");
        m_selection.StopAnimation(L"Size");
        m_selection.Offset(offset);
        m_selection.Size(size);
    }
    for (size_t i = 0; i < m_labels.size(); ++i)
    {
        const float opacity = static_cast<int>(i) == m_selected ? 1.0f : 0.65f;
        if (animate)
        {
            Motion::EaseTo(m_labels[i]->Visual(), L"Opacity", opacity, 180);
        }
        else
        {
            SetOpacityNow(m_labels[i]->Visual(), opacity);
        }
    }
}

void Segmented::Select(int index, bool animate)
{
    index = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    if (index != m_selected)
    {
        m_selected = index;
        PlaceSelection(animate);
    }
}

void Segmented::HighlightOption(int index)
{
    if (index == m_hoverOption)
    {
        return;
    }
    const int fade = m_ui.motion->hoverFadeMs;
    if (m_hoverOption >= 0)
    {
        Motion::EaseTo(m_highlights[m_hoverOption].visual, L"Opacity", 0.0f, fade);
    }
    m_hoverOption = index;
    if (index >= 0)
    {
        Motion::EaseTo(m_highlights[index].visual, L"Opacity", 1.0f, fade);
    }
}

void Segmented::OnHoverChanged(bool hovered)
{
    if (!hovered)
    {
        HighlightOption(-1);
    }
}

void Segmented::OnPointerMove(float x, float)
{
    HighlightOption(OptionAt(x));
}

void Segmented::OnPointerDown(float x, float)
{
    m_pressOption = OptionAt(x);
    Motion::SpringTo(m_labels[m_pressOption]->Visual(), L"Scale", float3{ 0.92f, 0.92f, 1.0f }, m_ui.motion->press);
}

void Segmented::OnPointerDrag(float x, float)
{
    // The press follows the pointer between options, like iOS.
    const int option = OptionAt(x);
    if (m_pressOption >= 0 && option != m_pressOption)
    {
        Motion::SpringTo(m_labels[m_pressOption]->Visual(), L"Scale", float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
        m_pressOption = option;
        Motion::SpringTo(m_labels[option]->Visual(), L"Scale", float3{ 0.92f, 0.92f, 1.0f }, m_ui.motion->press);
    }
    HighlightOption(option);
}

void Segmented::OnPressChanged(bool pressed)
{
    if (!pressed && m_pressOption >= 0)
    {
        Motion::SpringTo(m_labels[m_pressOption]->Visual(), L"Scale", float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
    }
}

void Segmented::OnPointerUp(bool inside)
{
    const int option = m_pressOption;
    m_pressOption = -1;
    if (inside && option >= 0 && option != m_selected)
    {
        Select(option);
        if (onChange)
        {
            onChange(option);
        }
    }
}

// --- Slider ------------------------------------------------------------------

Slider::Slider(const Ui& ui) : Control(ui), m_bubbleText(ui, 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)
{
    // A thick bar with no knob: how much of it is filled is the value.
    // Even pixel sizes keep it centred on whole pixels, so its edges stay crisp.
    m_thin = ui.EvenPx(8.0f);
    m_thick = ui.EvenPx(10.0f);   // hovered
    m_drag = ui.EvenPx(14.0f);    // held

    // Two numbers drive everything: Value (0..1) and the bar's Thickness.
    // The fill and the bar's outline follow them through expressions, so
    // one spring moves both together.
    m_properties = ui.compositor.CreatePropertySet();
    m_properties.InsertScalar(L"Value", 0.0f);
    m_properties.InsertScalar(L"Thickness", m_thin);

    m_track = ui.compositor.CreateContainerVisual();
    m_trackShape = ui.compositor.CreateRoundedRectangleGeometry();
    m_track.Clip(ui.compositor.CreateGeometricClip(m_trackShape));
    m_trackBackground = ui.compositor.CreateSpriteVisual();
    m_trackBackground.Brush(ui.compositor.CreateColorBrush(White(0.2f)));
    m_fill = ui.compositor.CreateSpriteVisual();
    m_fill.Brush(ui.compositor.CreateColorBrush(White(0.92f)));
    m_fill.Opacity(0.9f);
    m_track.Children().InsertAtTop(m_trackBackground);
    m_track.Children().InsertAtTop(m_fill);
    m_root.Children().InsertAtTop(m_track);

    // The value bubble, sized for the widest text ("100 %").
    m_bubble = ui.compositor.CreateContainerVisual();
    m_bubble.Opacity(0.0f);
    m_bubbleText.SetText(L"100 %");
    const float bubbleWidth = std::round(m_bubbleText.Width() + ui.Px(16.0f));
    const float bubbleHeight = ui.EvenPx(22.0f);
    m_bubble.Size({ bubbleWidth, bubbleHeight });
    m_bubble.CenterPoint({ bubbleWidth / 2.0f, bubbleHeight, 0.0f });  // it grows out of the bar
    m_bubbleBackground = RoundedRect(ui, bubbleWidth, bubbleHeight, bubbleHeight / 2.0f, { 240, 30, 30, 34 });
    m_bubble.Children().InsertAtTop(m_bubbleBackground.visual);
    m_bubble.Children().InsertAtTop(m_bubbleText.Visual());
    m_root.Children().InsertAtTop(m_bubble);
}

void Slider::Layout()
{
    const auto compositor = m_ui.compositor;
    m_track.Size({ m_width, m_height });
    m_trackBackground.Size({ m_width, m_height });
    m_fill.Size({ m_width, m_height });

    auto expression = [&](const wchar_t* formula)
    {
        auto animation = compositor.CreateExpressionAnimation(formula);
        animation.SetReferenceParameter(L"p", m_properties);
        animation.SetScalarParameter(L"W", m_width);
        animation.SetScalarParameter(L"H", m_height);
        return animation;
    };
    // The bar is centred in the control's height; the fill runs from its
    // left end to Value. The rounded clip shapes both ends.
    m_trackShape.StartAnimation(L"Offset", expression(L"Vector2(0, (H - p.Thickness) / 2)"));
    m_trackShape.StartAnimation(L"Size", expression(L"Vector2(W, p.Thickness)"));
    m_trackShape.StartAnimation(L"CornerRadius", expression(L"Vector2(p.Thickness / 2, p.Thickness / 2)"));
    m_fill.StartAnimation(L"Scale", expression(L"Vector3(p.Value, 1, 1)"));
    // The bubble rides above the end of the fill, kept inside the bar's ends.
    auto bubble = expression(L"Vector3(Clamp(p.Value * W - B / 2, -B / 4, W - B * 3 / 4), -B2 - G, 0)");
    bubble.SetScalarParameter(L"B", m_bubble.Size().x);
    bubble.SetScalarParameter(L"B2", m_bubble.Size().y);
    bubble.SetScalarParameter(L"G", m_ui.Px(6.0f));
    m_bubble.StartAnimation(L"Offset", bubble);
}

float Slider::ValueAt(float x) const
{
    return std::clamp(x / std::max(1.0f, m_width), 0.0f, 1.0f);
}

void Slider::Show(float value, bool animate)
{
    if (animate)
    {
        Motion::SpringTo(m_properties, L"Value", value, m_ui.motion->slider);
    }
    else
    {
        m_properties.StopAnimation(L"Value");
        m_properties.InsertScalar(L"Value", value);
    }
}

void Slider::SetValue(float value, bool animate)
{
    value = std::clamp(value, 0.0f, 1.0f);
    if (m_dragging || std::abs(value - m_value) < 0.0005f)
    {
        return;
    }
    m_value = value;
    Show(value, animate);
}

void Slider::UpdateLook()
{
    // Hovered: the bar grows a little and the fill turns fully bright. Held:
    // it grows more, and the bubble with the value pops up above it.
    const bool active = m_hovered || m_dragging;
    Motion::SpringTo(m_properties, L"Thickness", m_dragging ? m_drag : active ? m_thick : m_thin, m_ui.motion->slider);
    Motion::EaseTo(m_fill, L"Opacity", active ? 1.0f : 0.9f, m_ui.motion->hoverFadeMs);
    if (m_dragging)
    {
        UpdateBubble();
        Motion::EaseTo(m_bubble, L"Opacity", 1.0f, 90);
        m_bubble.Scale({ 0.6f, 0.6f, 1.0f });
        Motion::SpringTo(m_bubble, L"Scale", float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
    }
    else
    {
        // It stays a moment after letting go, so you see where it ended.
        Motion::EaseTo(m_bubble, L"Opacity", 0.0f, 160, 350);
    }
}

void Slider::UpdateBubble()
{
    const int value = static_cast<int>(std::lround(m_value * 100.0f));
    if (value == m_bubbleValue)
    {
        return;
    }
    m_bubbleValue = value;
    m_bubbleText.SetText(std::to_wstring(value) + L" %");
    const auto size = m_bubble.Size();
    m_bubbleText.SetPosition(std::round((size.x - m_bubbleText.Width()) / 2.0f), std::round((size.y - m_bubbleText.Height()) / 2.0f));
}

void Slider::OnHoverChanged(bool hovered)
{
    m_hovered = hovered;
    UpdateLook();
}

void Slider::OnPressChanged(bool pressed)
{
    m_dragging = pressed;
    UpdateLook();
}

void Slider::OnPointerDown(float x, float)
{
    // A click on the track glides there.
    m_dragging = true;
    m_value = ValueAt(x);
    Show(m_value, true);
    if (onChange)
    {
        onChange(m_value, false);
    }
}

void Slider::OnPointerDrag(float x, float)
{
    // Dragging follows the mouse exactly: no animation, no delay.
    const float value = ValueAt(x);
    if (value != m_value)
    {
        m_value = value;
        Show(value, false);
        UpdateBubble();
        if (onChange)
        {
            onChange(value, false);
        }
    }
}

void Slider::OnPointerUp(bool)
{
    m_dragging = false;
    if (onChange)
    {
        onChange(m_value, true);
    }
}

bool Slider::OnWheel(float notches)
{
    const float value = std::clamp(m_value + notches * 0.04f, 0.0f, 1.0f);
    if (value != m_value)
    {
        m_value = value;
        Show(value, true);
        if (onChange)
        {
            onChange(value, true);
        }
    }
    return true;
}

// --- TextButton --------------------------------------------------------------

TextButton::TextButton(const Ui& ui, float sizeDip, DWRITE_FONT_WEIGHT weight)
    : Control(ui), m_label(ui, sizeDip, weight)
{
    m_highlight = RoundedRect(ui, 1.0f, 1.0f, 1.0f, White(0.12f));
    m_highlight.visual.Opacity(0.0f);
    m_root.Children().InsertAtTop(m_highlight.visual);
    m_root.Children().InsertAtTop(m_label.Visual());
}

float TextButton::SetText(std::wstring_view text, float maxWidthPx)
{
    const float padX = m_ui.Px(6.0f);
    m_label.SetMaxWidth(std::max(1.0f, maxWidthPx - 2.0f * padX));
    m_label.SetText(text);
    const float width = text.empty() ? 0.0f : m_label.Width() + 2.0f * padX;
    SetBounds(m_x, m_y, width, m_label.Height() + m_ui.Px(2.0f));
    return width;
}

void TextButton::SetTextOpacity(float opacity)
{
    m_label.Visual().Opacity(opacity);
}

void TextButton::Layout()
{
    m_highlight.Resize(m_width, m_height, m_height / 2.0f);
    m_label.SetPosition(m_ui.Px(6.0f), std::round((m_height - m_label.Height()) / 2.0f));
}

void TextButton::OnHoverChanged(bool hovered)
{
    Motion::EaseTo(m_highlight.visual, L"Opacity", hovered ? 1.0f : 0.0f, m_ui.motion->hoverFadeMs);
}

void TextButton::OnPressChanged(bool pressed)
{
    Motion::SpringTo(m_root, L"Scale", pressed ? float3{ 0.95f, 0.95f, 1.0f } : float3{ 1.0f, 1.0f, 1.0f }, m_ui.motion->press);
}

void TextButton::OnClick()
{
    if (onClick)
    {
        onClick();
    }
}

// --- Artwork -----------------------------------------------------------------

Artwork::Artwork(const Ui& ui, float sizeDip, float radiusDip)
    : m_ui(ui), m_size(ui.EvenPx(sizeDip)), m_note(ui, sizeDip * 0.38f, DWRITE_FONT_WEIGHT_NORMAL, true)
{
    const float radius = ui.Px(radiusDip);
    m_root = ui.compositor.CreateContainerVisual();
    m_root.Size({ m_size, m_size });

    m_placeholder = RoundedRect(ui, m_size, m_size, radius, White(0.12f));
    m_root.Children().InsertAtTop(m_placeholder.visual);
    m_note.SetText(L"\xE8D6");  // a music note
    m_note.SetPosition(std::round((m_size - m_note.Width()) / 2.0f), std::round((m_size - m_note.Height()) / 2.0f));
    m_note.Visual().Opacity(0.7f);
    m_root.Children().InsertAtTop(m_note.Visual());

    // The picture is cut to the same rounded shape.
    auto shape = ui.compositor.CreateRoundedRectangleGeometry();
    shape.Size({ m_size, m_size });
    shape.CornerRadius({ radius, radius });
    auto images = ui.compositor.CreateContainerVisual();
    images.Size({ m_size, m_size });
    images.Clip(ui.compositor.CreateGeometricClip(shape));
    for (int i = 0; i < 2; ++i)
    {
        m_surfaces[i] = ui.graphics->CreateSurface();
        auto brush = ui.compositor.CreateSurfaceBrush(m_surfaces[i]);
        brush.Stretch(wuc::CompositionStretch::Fill);
        m_images[i] = ui.compositor.CreateSpriteVisual();
        m_images[i].Size({ m_size, m_size });
        m_images[i].Brush(brush);
        m_images[i].Opacity(0.0f);
        images.Children().InsertAtTop(m_images[i]);
    }
    m_root.Children().InsertAtTop(images);
}

void Artwork::SetPosition(float x, float y)
{
    m_root.Offset({ std::round(x), std::round(y), 0.0f });
}

void Artwork::SetImage(IWICBitmapSource* image, uint64_t id)
{
    if (id == m_id)
    {
        return;
    }
    m_id = id;
    const int fade = 220;
    if (!image)
    {
        Motion::EaseTo(m_images[0], L"Opacity", 0.0f, fade);
        Motion::EaseTo(m_images[1], L"Opacity", 0.0f, fade);
        return;
    }

    // Draw into the hidden layer, then crossfade to it.
    const int back = 1 - m_front;
    m_ui.graphics->Draw(m_surfaces[back], m_size, m_size, [&](ID2D1DeviceContext* context)
    {
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(context->CreateBitmapFromWicBitmap(image, nullptr, bitmap.put())))
        {
            const D2D1_RECT_F target{ 0.0f, 0.0f, m_size, m_size };
            context->DrawBitmap(bitmap.get(), &target, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
    });
    m_images[back].StopAnimation(L"Opacity");
    m_images[back].Opacity(0.0f);
    Motion::EaseTo(m_images[back], L"Opacity", 1.0f, fade);
    Motion::EaseTo(m_images[m_front], L"Opacity", 0.0f, fade, fade / 2);
    m_front = back;
}

// --- ProgressBar -------------------------------------------------------------

ProgressBar::ProgressBar(const Ui& ui)
{
    m_root = ui.compositor.CreateContainerVisual();
    m_shape = ui.compositor.CreateRoundedRectangleGeometry();
    m_root.Clip(ui.compositor.CreateGeometricClip(m_shape));
    m_track = ui.compositor.CreateSpriteVisual();
    m_track.Brush(ui.compositor.CreateColorBrush(White(0.2f)));
    m_fill = ui.compositor.CreateSpriteVisual();
    m_fill.Brush(ui.compositor.CreateColorBrush(White(0.9f)));
    m_fill.Scale({ 0.0f, 1.0f, 1.0f });
    m_root.Children().InsertAtTop(m_track);
    m_root.Children().InsertAtTop(m_fill);
}

void ProgressBar::SetBounds(float x, float y, float width, float height)
{
    m_root.Offset({ std::round(x), std::round(y), 0.0f });
    m_root.Size({ width, height });
    m_shape.Size({ width, height });
    m_shape.CornerRadius({ height / 2.0f, height / 2.0f });
    m_track.Size({ width, height });
    m_fill.Size({ width, height });
}

void ProgressBar::Show(double fraction, bool running, double secondsLeft)
{
    const float start = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    m_fill.StopAnimation(L"Scale.X");
    m_fill.Scale({ start, 1.0f, 1.0f });
    if (running && secondsLeft > 0.05 && start < 1.0f)
    {
        // A linear glide from here to the end, timed to the rest of the song.
        const auto compositor = m_fill.Compositor();
        auto animation = compositor.CreateScalarKeyFrameAnimation();
        const auto linear = compositor.CreateLinearEasingFunction();
        animation.InsertKeyFrame(0.0f, start, linear);
        animation.InsertKeyFrame(1.0f, 1.0f, linear);
        animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(secondsLeft)));
        m_fill.StartAnimation(L"Scale.X", animation);
    }
}

void ProgressBar::Freeze()
{
    m_fill.StopAnimation(L"Scale.X");  // keeps the value it had reached
}
