#pragma once

#include "views/View.h"

// What the small resting pill shows. Empty glass for now; Phase 4 adds the
// now-playing artwork.
class IdleView : public View
{
public:
    using View::View;
    void Build() override;
};
