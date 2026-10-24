#pragma once

#include <array>
#include <string>

#include <winrt/Windows.System.h>

#include "services/BluetoothService.h"
#include "services/RadioService.h"
#include "services/WifiService.h"
#include "views/sections/Section.h"

// The "Connections" column: Wi-Fi and Bluetooth tiles, and under them the
// list for the selected one: Wi-Fi networks to join, paired Bluetooth
// devices to connect, or new devices to pair.
class ConnectionsSection : public Section
{
public:
    static constexpr int kRows = 4;

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

    RadiosState m_radios;
    WifiState m_wifi;
    BluetoothState m_bluetooth;
    List m_list = List::Wifi;
    bool m_visible = false;
    bool m_built = false;
    bool m_wifiScanned = false;  // a scan has finished since Island started

    std::unique_ptr<ToggleTile> m_wifiTile, m_bluetoothTile;
    std::array<std::unique_ptr<ListRow>, kRows> m_rows;
    std::array<std::wstring, kRows> m_rowIds;  // the network name or device id each row shows
    std::unique_ptr<ListRow> m_footer;
    std::unique_ptr<Spotlight> m_spotlight;
    int m_page = 0;      // when the list is longer than the rows
    int m_moreRow = -1;  // the row that shows the next page, if any
    winrt::Windows::System::DispatcherQueueTimer m_rescan{ nullptr };
};

// Which of `count` lines a list with `rows` rows shows on `page`. When they
// don't all fit, the last row becomes a "more" row that steps to the next
// page, and from the last page back to the first. Pure logic, unit-tested.
struct ListPage
{
    int page = 0;   // `page`, wrapped into range
    int first = 0;  // index of the first line shown
    int count = 0;  // lines shown
    bool more = false;
};
ListPage PageLines(int count, int rows, int page);
