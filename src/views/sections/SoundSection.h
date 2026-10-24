#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "services/AudioService.h"
#include "views/sections/Section.h"

// The "Sound" column: the main volume (with the output device's name) and a
// mixer with one slider per app that's playing sound.
class SoundSection : public Section
{
public:
    static constexpr int kAppRows = 4;

    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void SetAudio(const AudioState& audio, bool animate);

private:
    void Apply(bool animate);
    void AssignRows();

    AudioState m_audio;
    SliderRow m_master;
    std::array<SliderRow, kAppRows> m_apps;
    std::array<std::wstring, kAppRows> m_rowKeys;  // which app each row shows ("" = none)
    std::unique_ptr<Label> m_hint;
};

// Which app goes in which mixer row. An app keeps its row while it's in the
// list, so rows don't swap under the mouse; new apps fill free rows in the
// order given (the most important first); apps that don't fit are left out.
// Pure logic, unit-tested.
template <size_t N>
void AssignMixerRows(std::array<std::wstring, N>& rows, const std::vector<std::wstring>& apps)
{
    for (std::wstring& row : rows)
    {
        if (std::find(apps.begin(), apps.end(), row) == apps.end())
        {
            row.clear();
        }
    }
    for (const std::wstring& app : apps)
    {
        if (std::find(rows.begin(), rows.end(), app) != rows.end())
        {
            continue;
        }
        const auto free = std::find(rows.begin(), rows.end(), std::wstring());
        if (free == rows.end())
        {
            break;
        }
        *free = app;
    }
}
