#include "views/IdleView.h"

#include "platform/Settings.h"

void IdleView::Build()
{
    const LayoutSettings& layout = *m_ui.layout;
    ResetLayer(m_ui.EvenPx(layout.idleWidth), m_ui.Px(layout.idleHeight));
}
