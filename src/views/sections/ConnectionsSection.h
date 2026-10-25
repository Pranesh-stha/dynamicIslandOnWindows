#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <winrt/Windows.System.h>

#include "services/BluetoothService.h"
#include "services/RadioService.h"
#include "services/WifiService.h"
#include "views/sections/Section.h"

// The "Connections" column: Wi-Fi and Bluetooth tiles, and under them the
// list for the selected one: Wi-Fi networks to join, paired Bluetooth
// devices to connect, or new devices to pair. A list longer than its four
// rows scrolls with the mouse wheel.
class ConnectionsSection : public Section
{
public:
    static constexpr int kRows = 4;        // rows on screen
    static constexpr int kMaxLines = 30;   // longest list (Wi-Fi: the strongest networks come first)

    using Section::Section;

    float Build(const winrt::Windows::UI::Composition::ContainerVisual& layer, ControlHost& controls,
                float x, float y, float width) override;
    void OnVisibilityChanged(bool visible) override;
    void SetRadios(const RadiosState& radios, bool animate);
    void SetWifi(const WifiState& wifi, bool animate);
    void SetBluetooth(const BluetoothState& bluetooth, bool animate);

private:
    enum class List { Wifi, Bluetooth, AddDevice };

    void Show(List list);
    void ApplyTiles(bool animate);
    void ApplyList(bool animate);
    void OnRowClicked(int row);
    void StartRefreshing();

    // Scrolling (see Build).
    ListRow& Row(int index);  // made when a list first needs it
    bool OnWheel(float notches);
    void ScrollTo(int first, bool animate);
    void ResetScroll();  // back to the top, at once
    void StopSlide();
    void PlaceRows(bool animate);  // each row's place, which ones take the mouse, the scroll bar
    Motion::SpringPoint Slide() const;  // where the rows' picture is in its slide right now

    RadiosState m_radios;
    WifiState m_wifi;
    BluetoothState m_bluetooth;
    List m_list = List::Wifi;
    bool m_visible = false;
    bool m_built = false;
    bool m_wifiScanned = false;  // a scan has finished since Island started

    std::unique_ptr<ToggleTile> m_wifiTile, m_bluetoothTile;
    std::vector<std::unique_ptr<ListRow>> m_rows;  // one per line of the longest list so far
    std::vector<std::wstring> m_rowIds;            // the network name or device id each row shows
    std::vector<char> m_rowClickable;
    std::unique_ptr<ListRow> m_footer;
    std::unique_ptr<Spotlight> m_spotlight;
    winrt::Windows::System::DispatcherQueueTimer m_rescan{ nullptr };

    ControlHost* m_controls = nullptr;
    winrt::Windows::UI::Composition::ContainerVisual m_viewport{ nullptr };  // clipped to the list's four rows
    winrt::Windows::UI::Composition::ContainerVisual m_content{ nullptr };   // the rows; slides when scrolling
    std::unique_ptr<WheelArea> m_wheelArea;
    RoundedRect m_thumb;  // the thin scroll bar at the right edge
    float m_listX = 0.0f, m_listTop = 0.0f, m_listWidth = 0.0f, m_listHeight = 0.0f;
    float m_rowHeight = 0.0f, m_rowStep = 0.0f;
    int m_lineCount = 0;
    int m_scroll = 0;       // the first line on screen
    float m_wheel = 0.0f;   // wheel notches not yet turned into whole rows (touchpads send small steps)
    float m_slideFrom = 0.0f, m_slideVelocity = 0.0f;  // the slide: from this offset (px) to 0, starting at this speed
    uint64_t m_slideStart = 0;
};

// The first of `count` lines a list with `rows` rows can show when asked to
// start at `first`: never past the end, so the last rows stay full.
// Pure logic, unit-tested.
int ScrollFirst(int count, int rows, int first);
