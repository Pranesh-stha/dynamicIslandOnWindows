#include "views/PeekView.h"

#include "platform/Settings.h"
#include "render/Glyphs.h"

void PeekView::Build()
{
    const LayoutSettings& layout = *m_ui.layout;
    ResetLayer(m_ui.EvenPx(layout.peekWidth), m_ui.Px(layout.peekHeight));

    const float pad = m_ui.Px(10.0f);
    const float badge = m_ui.EvenPx(36.0f);
    const float badgeY = std::round((m_height - badge) / 2.0f);
    m_badge = std::make_unique<RoundedRect>(m_ui, badge, badge, badge / 2.0f, White(0.14f));
    m_badge->visual.Offset({ pad, badgeY, 0.0f });
    m_layer.Children().InsertAtTop(m_badge->visual);

    m_badgeIcon = std::make_unique<Label>(m_ui, 16.0f, DWRITE_FONT_WEIGHT_NORMAL, true);
    m_badgeIcon->SetText(std::wstring(1, Glyph::MusicNote));
    m_badgeIcon->SetPosition(pad + std::round((badge - m_badgeIcon->Width()) / 2.0f),
                             badgeY + std::round((badge - m_badgeIcon->Height()) / 2.0f));
    m_layer.Children().InsertAtTop(m_badgeIcon->Visual());

    const float textX = pad + badge + m_ui.Px(10.0f);
    const float textWidth = m_width - textX - m_ui.Px(20.0f);
    m_title = std::make_unique<Label>(m_ui, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_title->SetMaxWidth(textWidth);
    m_title->SetText(L"Nothing playing");
    m_subtitle = std::make_unique<Label>(m_ui, 11.5f);
    m_subtitle->SetMaxWidth(textWidth);
    m_subtitle->SetText(L"Click for the control center");
    m_subtitle->Visual().Opacity(0.6f);

    const float textTop = std::round((m_height - m_title->Height() - m_subtitle->Height()) / 2.0f);
    m_title->SetPosition(textX, textTop);
    m_subtitle->SetPosition(textX, textTop + m_title->Height());
    m_layer.Children().InsertAtTop(m_title->Visual());
    m_layer.Children().InsertAtTop(m_subtitle->Visual());
}
