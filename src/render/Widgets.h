#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>

#include "render/Graphics.h"
#include "render/Motion.h"

struct LayoutSettings;

// What every piece of UI needs to build itself.
struct Ui
{
    winrt::Windows::UI::Composition::Compositor compositor{ nullptr };
    Graphics* graphics = nullptr;
    const MotionSettings* motion = nullptr;
    const LayoutSettings* layout = nullptr;
    float scale = 1.0f;  // display scale: 1.25 at 125 %, 1.75 at 175 %
    winrt::Windows::UI::Color accent{ 255, 96, 205, 255 };

    // DIPs to whole pixels. Everything sits on whole pixels so it's sharp at rest.
    float Px(float dip) const { return std::round(dip * scale); }
    // The same, rounded to an even number, so centered things stay on whole pixels.
    float EvenPx(float dip) const { return 2.0f * std::round(dip * scale / 2.0f); }
};

winrt::Windows::UI::Color White(float opacity);
winrt::Windows::UI::Color Black(float opacity);

// A filled rounded rectangle (or circle) drawn by the compositor as a
// vector shape, so it stays crisp at any size and scale.
struct RoundedRect
{
    RoundedRect() = default;
    RoundedRect(const Ui& ui, float width, float height, float radius, winrt::Windows::UI::Color fill);
    void Resize(float width, float height, float radius);

    winrt::Windows::UI::Composition::ShapeVisual visual{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry geometry{ nullptr };
    winrt::Windows::UI::Composition::CompositionSpriteShape shape{ nullptr };
};

// One line of white text (or one icon), drawn once with DirectWrite and
// redrawn only when the text changes. Dim it with its visual's Opacity.
class Label
{
public:
    Label(const Ui& ui, float sizeDip, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, bool icon = false);

    winrt::Windows::UI::Composition::SpriteVisual Visual() const { return m_visual; }
    void SetText(std::wstring_view text);
    void SetMaxWidth(float widthPx);  // longer text ends in "…"
    void SetPosition(float x, float y);  // top-left corner, in pixels
    float Width() const { return std::max(0.0f, m_size.Width - 2.0f); }
    float Height() const;
    const std::wstring& Text() const { return m_text; }

private:
    void Render();

    const Ui& m_ui;
    TextStyle m_style;
    std::wstring m_text;
    winrt::Windows::UI::Composition::SpriteVisual m_visual{ nullptr };
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_surface{ nullptr };
    Graphics::Size m_size{};
};

// Base class for everything you can hover and click. Each control is its own
// group of GPU visuals, and every state change (hover, press, on/off,
// disabled) is an animation, never a jump.
class Control
{
public:
    explicit Control(const Ui& ui);
    virtual ~Control() = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    winrt::Windows::UI::Composition::ContainerVisual Visual() const { return m_root; }

    // Position and size in the parent layer, in pixels.
    void SetBounds(float x, float y, float width, float height);
    float X() const { return m_x; }
    float Y() const { return m_y; }
    float Width() const { return m_width; }
    float Height() const { return m_height; }
    bool Contains(float x, float y) const;

    bool Enabled() const { return m_enabled; }
    void SetEnabled(bool enabled);

    // Pointer input, sent by ControlHost. Coordinates are relative to the control.
    // Feedback starts on press; the action happens on release, and only if the
    // pointer is still on the control (dragging off cancels, as on macOS/iOS).
    virtual void OnHoverChanged(bool /*hovered*/) {}
    virtual void OnPressChanged(bool /*pressed*/) {}
    virtual void OnPointerMove(float /*x*/, float /*y*/) {}
    virtual void OnPointerDown(float /*x*/, float /*y*/) {}
    virtual void OnPointerDrag(float /*x*/, float /*y*/) {}
    virtual void OnPointerUp(bool inside) { if (inside) OnClick(); }
    virtual void OnClick() {}
    virtual void OnRightClick() {}
    virtual bool OnWheel(float /*notches*/) { return false; }
    virtual bool KeepsPressOutside() const { return false; }  // sliders keep dragging outside

protected:
    virtual void Layout() {}  // called after SetBounds

    const Ui& m_ui;
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    float m_x = 0.0f, m_y = 0.0f, m_width = 0.0f, m_height = 0.0f;
    bool m_enabled = true;
};

// Routes the mouse to the right control inside one layer: hover, press,
// drag, release, right-click and wheel. It doesn't own the controls.
class ControlHost
{
public:
    void Add(Control& control) { m_controls.push_back(&control); }
    void Clear();

    // Coordinates are in the layer's pixels.
    void PointerMove(float x, float y);
    bool PointerDown(float x, float y);  // true if a control took the press
    void PointerUp(float x, float y);
    void PointerLeave();
    void CancelPress();
    bool RightClick(float x, float y);
    bool Wheel(float x, float y, float notches);
    bool Pressing() const { return m_pressed != nullptr; }

private:
    Control* Hit(float x, float y) const;
    void SetHovered(Control* control);

    std::vector<Control*> m_controls;
    Control* m_hovered = nullptr;
    Control* m_pressed = nullptr;
    bool m_pressedInside = false;
};

// A round icon button: a soft highlight circle fades in on hover, it shrinks
// a little on press and springs back with a tiny overshoot.
class IconButton : public Control
{
public:
    IconButton(const Ui& ui, wchar_t glyph, float diameterDip, float glyphDip);

    // morph = true: the old icon turns and shrinks away while the new one
    // turns in (play <-> pause). Otherwise a plain crossfade.
    void SetGlyph(wchar_t glyph, bool morph);

    std::function<void()> onClick;
    std::function<void()> onRightClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;
    void OnRightClick() override;

protected:
    void Layout() override;

private:
    void CenterGlyph(Label& glyph);
    float RestingGlyphOpacity() const;

    RoundedRect m_highlight;
    std::unique_ptr<Label> m_glyphs[2];
    uint64_t m_hiddenAt[2]{};
    int m_front = 0;
    wchar_t m_glyph;
    bool m_hovered = false;
    bool m_pressed = false;
};

// The on/off switch drawn inside a ToggleTile: the knob slides with a soft
// spring, the track colour crossfades, and the knob stretches a little
// while held (like iOS).
class ToggleSwitch
{
public:
    explicit ToggleSwitch(const Ui& ui);

    winrt::Windows::UI::Composition::ContainerVisual Visual() const { return m_root; }
    float Width() const { return m_width; }
    float Height() const { return m_height; }

    void SetOn(bool on, bool animate);
    void SetHovered(bool hovered);
    void SetPressed(bool pressed);
    void SetBusy(bool busy);  // a gentle pulse while Windows is still switching

private:
    void PlaceKnob(bool animate);

    const Ui& m_ui;
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    RoundedRect m_off, m_on, m_hover;
    winrt::Windows::UI::Composition::ShapeVisual m_knobLayer{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_knob{ nullptr };
    float m_width, m_height, m_pad, m_knobSize, m_knobStretched;
    bool m_isOn = false;
    bool m_pressed = false;
    bool m_busy = false;
};

// A tile with an icon, a title, a subtitle and a switch (Wi-Fi, Bluetooth).
// Clicking anywhere on it toggles; right-click is for "open Settings".
class ToggleTile : public Control
{
public:
    ToggleTile(const Ui& ui, wchar_t glyph, std::wstring_view title);

    void SetOn(bool on, bool animate = true);
    bool IsOn() const { return m_isOn; }
    void SetSubtitle(std::wstring_view text);
    void SetBusy(bool busy) { m_switch.SetBusy(busy); }

    std::function<void(bool on)> onToggle;
    std::function<void()> onRightClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;
    void OnRightClick() override;

protected:
    void Layout() override;

private:
    RoundedRect m_background, m_hoverLayer, m_iconOff, m_iconOn;
    Label m_icon, m_title, m_subtitle;
    ToggleSwitch m_switch;
    bool m_isOn = false;
};

// A row of choices with a selection pill that slides to the chosen one
// (e.g. Extend / Duplicate).
class Segmented : public Control
{
public:
    Segmented(const Ui& ui, std::vector<std::wstring> options);

    void Select(int index, bool animate = true);
    int Selected() const { return m_selected; }

    std::function<void(int index)> onChange;

    void OnHoverChanged(bool hovered) override;
    void OnPointerMove(float x, float y) override;
    void OnPointerDown(float x, float y) override;
    void OnPointerDrag(float x, float y) override;
    void OnPressChanged(bool pressed) override;
    void OnPointerUp(bool inside) override;

protected:
    void Layout() override;

private:
    int OptionAt(float x) const;
    void HighlightOption(int index);
    void PlaceSelection(bool animate);

    std::vector<std::wstring> m_options;
    RoundedRect m_background;
    winrt::Windows::UI::Composition::ShapeVisual m_selectionLayer{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_selection{ nullptr };
    std::vector<RoundedRect> m_highlights;
    std::vector<std::unique_ptr<Label>> m_labels;
    int m_selected = 0;
    int m_hoverOption = -1;
    int m_pressOption = -1;
};

// A slider (volume, brightness). While dragging it follows the mouse 1:1;
// a click on the track, the mouse wheel and outside changes glide there with
// a spring. Hovering thickens the track and grows the thumb.
class Slider : public Control
{
public:
    explicit Slider(const Ui& ui);

    // Ignored while you're dragging it, so echoes of our own changes can't
    // make it jitter.
    void SetValue(float value, bool animate = true);
    float Value() const { return m_value; }
    bool Dragging() const { return m_dragging; }

    // final = false while dragging; true when the value is settled.
    std::function<void(float value, bool final)> onChange;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnPointerDown(float x, float y) override;
    void OnPointerDrag(float x, float y) override;
    void OnPointerUp(bool inside) override;
    bool OnWheel(float notches) override;
    bool KeepsPressOutside() const override { return true; }

protected:
    void Layout() override;

private:
    float ValueAt(float x) const;
    void Show(float value, bool animate);
    void UpdateThumb();

    winrt::Windows::UI::Composition::CompositionPropertySet m_properties{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_track{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_trackShape{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_trackBackground{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_fill{ nullptr };
    winrt::Windows::UI::Composition::ShapeVisual m_thumb{ nullptr };
    float m_thumbSize, m_thin, m_thick;
    float m_value = 0.0f;
    bool m_dragging = false;
    bool m_hovered = false;
};
