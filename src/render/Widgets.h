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
    float screenWidth = 0.0f;  // width of the island's monitor, in pixels
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
    void SetMaxLines(int lines);      // with a max width: wrap onto this many lines (default 1)
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

    // Not interactive: ignores the mouse but, unlike disabled, isn't dimmed.
    bool Interactive() const { return m_interactive; }
    void SetInteractive(bool interactive) { m_interactive = interactive; }

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
    virtual void OnRightClick() { if (onRightClick) onRightClick(); }

    // What a right-click does (e.g. open the matching Settings page).
    std::function<void()> onRightClick;
    virtual bool OnWheel(float /*notches*/) { return false; }
    virtual bool KeepsPressOutside() const { return false; }  // sliders keep dragging outside

protected:
    virtual void Layout() {}  // called after SetBounds

    const Ui& m_ui;
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    float m_x = 0.0f, m_y = 0.0f, m_width = 0.0f, m_height = 0.0f;
    bool m_enabled = true;
    bool m_interactive = true;
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
    // turns in (play <-> pause). Otherwise a plain crossfade. With animate =
    // false the new icon just appears (for content that isn't visible).
    void SetGlyph(wchar_t glyph, bool morph, bool animate = true);

    std::function<void()> onClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;

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
// Without onSelect, a click anywhere toggles. With onSelect, only the
// switch toggles and the rest of the tile selects it (e.g. to show its
// list of networks). Right-click is for "open Settings".
class ToggleTile : public Control
{
public:
    ToggleTile(const Ui& ui, wchar_t glyph, std::wstring_view title);

    void SetOn(bool on, bool animate = true);
    bool IsOn() const { return m_isOn; }
    void SetSubtitle(std::wstring_view text);
    void SetBusy(bool busy);
    void SetSelected(bool selected, bool animate = true);

    // Compact: no switch. The round icon turns it on and off (like macOS'
    // Control Center) and the rest of the tile selects it, so two tiles fit
    // side by side. Set before SetBounds.
    void SetCompact(bool compact);

    std::function<void(bool on)> onToggle;
    std::function<void()> onSelect;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnPointerDown(float x, float y) override;
    void OnClick() override;

protected:
    void Layout() override;

private:
    RoundedRect m_background, m_selectedLayer, m_hoverLayer, m_iconOff, m_iconOn;
    Label m_icon, m_title, m_subtitle;
    ToggleSwitch m_switch;
    float m_switchX = 0.0f;
    bool m_isOn = false;
    bool m_pressOnSwitch = false;
    bool m_compact = false;
    bool m_busy = false;
};

// A small pill with an icon and a word, on or off: the quick toggles at the
// top of the control center (microphone, night light, energy saver). On, it
// fills with a colour (the accent, unless SetOnColor says otherwise).
class ToggleChip : public Control
{
public:
    ToggleChip(const Ui& ui, wchar_t glyph, std::wstring_view label);

    void SetOn(bool on, bool animate);
    bool IsOn() const { return m_on; }
    void SetGlyph(wchar_t glyph);
    void SetOnColor(winrt::Windows::UI::Color color);
    void SetBusy(bool busy);   // a gentle pulse while Windows is switching
    float NaturalWidth() const;  // just fits the icon and the word

    std::function<void()> onClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;

protected:
    void Layout() override;

private:
    float FullBloom() const;

    RoundedRect m_background, m_onLayer, m_hoverLayer;
    winrt::Windows::UI::Composition::CompositionEllipseGeometry m_bloom{ nullptr };  // the "on" colour spreads from the icon
    Label m_icon, m_label;
    bool m_on = false;
    bool m_busy = false;
};

// One hover highlight shared by the rows of a list: it glides from row to
// row with the mouse instead of each row lighting up on its own (like macOS
// menus). Rows report hover to it (ListRow::SetSpotlight).
class Spotlight
{
public:
    explicit Spotlight(const Ui& ui);

    winrt::Windows::UI::Composition::ShapeVisual Visual() const { return m_rect.visual; }
    void MoveTo(float x, float y, float width, float height);  // the mouse entered a row
    void Leave();                                               // ...and left it
    void Hide();                                                // at once (the list is being hidden)

private:
    const Ui& m_ui;
    RoundedRect m_rect;
    bool m_shown = false;
    uint64_t m_leftAt = 0;
};

// One line of a list (a Wi-Fi network, a Bluetooth device, "Add a
// device"): an icon in a circle (accent coloured when active, e.g.
// connected), a title, a detail line and an optional small icon at the end.
class ListRow : public Control
{
public:
    explicit ListRow(const Ui& ui);

    // glyph/trailing 0 = none. Redraws only what changed.
    void SetContent(wchar_t glyph, std::wstring_view title, std::wstring_view detail, wchar_t trailing, bool active, bool animate);
    void SetBusy(bool busy);                  // a gentle pulse while something is in progress
    void SetShown(bool shown, bool animate);  // hidden rows don't take the mouse
    void SetSpotlight(Spotlight* spotlight) { m_spotlight = spotlight; }  // the list's shared highlight instead of the row's own

    std::function<void()> onClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;

protected:
    void Layout() override;

private:
    void Arrange();

    RoundedRect m_highlight, m_iconOff, m_iconOn;
    Label m_icon, m_title, m_detail, m_trailing;
    Spotlight* m_spotlight = nullptr;
    bool m_active = false;
    bool m_busy = false;
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
    void UpdateLook();
    void UpdateBubble();

    winrt::Windows::UI::Composition::CompositionPropertySet m_properties{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_track{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_trackShape{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_trackBackground{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_fill{ nullptr };
    // While dragging: a little pill above the end of the fill with the value ("72 %").
    winrt::Windows::UI::Composition::ContainerVisual m_bubble{ nullptr };
    RoundedRect m_bubbleBackground;
    Label m_bubbleText;
    int m_bubbleValue = -1;
    float m_thin, m_thick, m_drag;
    float m_value = 0.0f;
    bool m_dragging = false;
    bool m_hovered = false;
};

// A small clickable piece of text with a pill-shaped hover highlight (the
// app name under the song, which switches apps when several play media).
class TextButton : public Control
{
public:
    TextButton(const Ui& ui, float sizeDip, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

    // Sizes the button to fit the text; returns the new width in pixels.
    float SetText(std::wstring_view text, float maxWidthPx);
    void SetTextOpacity(float opacity);

    std::function<void()> onClick;

    void OnHoverChanged(bool hovered) override;
    void OnPressChanged(bool pressed) override;
    void OnClick() override;

protected:
    void Layout() override;

private:
    RoundedRect m_highlight;
    Label m_label;
};

// Album artwork: a rounded image that crossfades when the song changes,
// with a music-note placeholder while there's none.
class Artwork
{
public:
    Artwork(const Ui& ui, float sizeDip, float radiusDip);

    winrt::Windows::UI::Composition::ContainerVisual Visual() const { return m_root; }
    float Size() const { return m_size; }
    void SetPosition(float x, float y);

    // `id` identifies the picture; the same id again does nothing, so this
    // can be called on every update.
    void SetImage(IWICBitmapSource* image, uint64_t id);

private:
    const Ui& m_ui;
    float m_size;
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    RoundedRect m_placeholder;
    Label m_note;
    winrt::Windows::UI::Composition::SpriteVisual m_images[2]{ nullptr, nullptr };
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_surfaces[2]{ nullptr, nullptr };
    int m_front = 0;
    uint64_t m_id = ~0ull;
};

// A thin progress line. While playing, the compositor glides it to the end
// on its own; nothing is redrawn every second.
class ProgressBar
{
public:
    explicit ProgressBar(const Ui& ui);

    winrt::Windows::UI::Composition::ContainerVisual Visual() const { return m_root; }
    void SetBounds(float x, float y, float width, float height);

    // Shows `fraction` (0..1) now and, while `running`, moves linearly to the
    // end over `secondsLeft`.
    void Show(double fraction, bool running, double secondsLeft);
    // Stops moving where it is (when the panel closes: no frames while idle).
    void Freeze();

private:
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry m_shape{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_track{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_fill{ nullptr };
};
