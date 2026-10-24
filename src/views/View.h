#pragma once

#include "render/Widgets.h"

// One of the things the island shows: the idle pill's content, the Peek
// strip, the control center. Each view owns one layer of visuals, hung from
// the top center of the pill, which fades in when its state is shown.
class View
{
public:
    View(const Ui& ui, winrt::Windows::UI::Composition::ContainerVisual parent) : m_ui(ui), m_parent(std::move(parent)) {}
    virtual ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // (Re)creates every visual at the current display scale. Called at start
    // and after display-scale, settings or GPU changes.
    virtual void Build() = 0;

    // Old content fades out quickly; new content fades in partway through
    // the morph with a slight scale-up.
    void SetVisible(bool visible, bool animate);
    bool Visible() const { return m_visible; }

    // The island's resting size while this view is shown, in pixels.
    float Width() const { return m_width; }
    float Height() const { return m_height; }

    ControlHost& Controls() { return m_controls; }

protected:
    // Called when the view starts or stops being shown, e.g. to run the
    // progress bar only while it can be seen.
    virtual void OnVisibilityChanged(bool /*visible*/) {}

    // Replaces the old layer with a fresh, empty one of this size.
    void ResetLayer(float width, float height);
    // For views whose height follows their content: set it once laid out.
    void SetHeight(float height);

    const Ui& m_ui;
    winrt::Windows::UI::Composition::ContainerVisual m_parent{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_layer{ nullptr };
    ControlHost m_controls;
    float m_width = 0.0f;
    float m_height = 0.0f;
    bool m_visible = false;
};
