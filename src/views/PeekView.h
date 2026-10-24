#pragma once

#include <memory>

#include "views/View.h"

// The wider strip shown while the mouse rests on the island. Phase 4 fills
// it with what's playing; for now it says how to open the control center.
class PeekView : public View
{
public:
    using View::View;
    void Build() override;

private:
    std::unique_ptr<RoundedRect> m_badge;
    std::unique_ptr<Label> m_badgeIcon, m_title, m_subtitle;
};
