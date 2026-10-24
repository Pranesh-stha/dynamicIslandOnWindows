#include "views/sections/ConnectionsSection.h"

#include <algorithm>
#include <chrono>
#include <format>

#include "render/Glyphs.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace
{
    wchar_t SignalGlyph(int bars)
    {
        return bars >= 4 ? Glyph::Wifi : bars == 3 ? Glyph::Wifi3 : bars == 2 ? Glyph::Wifi2 : Glyph::Wifi1;
    }

    wchar_t DeviceGlyph(DeviceKind kind)
    {
        switch (kind)
        {
        case DeviceKind::Audio: return Glyph::Headphone;
        case DeviceKind::Mouse: return Glyph::Mouse;
        case DeviceKind::Keyboard: return Glyph::Keyboard;
        case DeviceKind::Phone: return Glyph::CellPhone;
        case DeviceKind::Computer: return Glyph::Laptop;
        case DeviceKind::Gamepad: return Glyph::GameController;
        default: return Glyph::Bluetooth;
        }
    }
}

ListPage PageLines(int count, int rows, int page)
{
    if (count <= rows)
    {
        return { 0, 0, count, false };
    }
    const int perPage = rows - 1;
    const int pages = (count + perPage - 1) / perPage;
    ListPage result;
    result.page = (page % pages + pages) % pages;
    result.first = result.page * perPage;
    result.count = std::min(perPage, count - result.first);
    result.more = true;
    return result;
}

float ConnectionsSection::Build(const wuc::ContainerVisual& layer, ControlHost& controls, float x, float y, float width)
{
    const Ui& ui = m_ui;
    y = AddTitle(layer, L"Connections", x, y);

    // The two tiles, side by side. The round icon turns the radio on and
    // off; the rest of the tile shows its list underneath.
    const float tileHeight = ui.Px(50.0f);
    const float tileGap = ui.Px(8.0f);
    const float tileWidth = std::floor((width - tileGap) / 2.0f);
    m_wifiTile = std::make_unique<ToggleTile>(ui, Glyph::Wifi, L"Wi-Fi");
    m_bluetoothTile = std::make_unique<ToggleTile>(ui, Glyph::Bluetooth, L"Bluetooth");
    m_wifiTile->SetCompact(true);
    m_bluetoothTile->SetCompact(true);
    m_wifiTile->SetBounds(x, y, tileWidth, tileHeight);
    m_bluetoothTile->SetBounds(x + width - tileWidth, y, tileWidth, tileHeight);
    m_wifiTile->onToggle = [this](bool on) { if (m_actions.setWifi) m_actions.setWifi(on); };
    m_bluetoothTile->onToggle = [this](bool on) { if (m_actions.setBluetooth) m_actions.setBluetooth(on); };
    m_wifiTile->onSelect = [this] { Show(List::Wifi); };
    m_bluetoothTile->onSelect = [this] { Show(m_list == List::AddDevice ? List::AddDevice : List::Bluetooth); };
    m_wifiTile->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:network-wifi"); };
    m_bluetoothTile->onRightClick = [this] { if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:bluetooth"); };
    Add(layer, controls, *m_wifiTile);
    Add(layer, controls, *m_bluetoothTile);
    y += tileHeight + ui.Px(10.0f);

    // The list: a few rows made once and filled as things change, and a footer.
    const float rowHeight = ui.Px(36.0f);
    const float rowGap = ui.Px(2.0f);
    for (int i = 0; i < kRows; ++i)
    {
        m_rows[i] = std::make_unique<ListRow>(ui);
        m_rows[i]->SetBounds(x, y, width, rowHeight);
        m_rows[i]->onClick = [this, i] { OnRowClicked(i); };
        m_rows[i]->onRightClick = [this]
        {
            if (m_actions.openSettings)
            {
                m_actions.openSettings(m_list == List::Wifi ? L"ms-settings:network-wifi" : L"ms-settings:bluetooth");
            }
        };
        Add(layer, controls, *m_rows[i]);
        y += rowHeight + rowGap;
    }
    m_footer = std::make_unique<ListRow>(ui);
    m_footer->SetBounds(x, y, width, rowHeight);
    m_footer->onClick = [this]
    {
        switch (m_list)
        {
        case List::Wifi:
            if (m_actions.openSettings) m_actions.openSettings(L"ms-settings:network-wifi");
            break;
        case List::Bluetooth:
            Show(List::AddDevice);
            break;
        case List::AddDevice:
            Show(List::Bluetooth);
            break;
        }
    };
    Add(layer, controls, *m_footer);
    y += rowHeight;

    m_built = true;
    ApplyTiles(false);
    ApplyList(false);
    return y;
}

void ConnectionsSection::Show(List list)
{
    if (list == m_list)
    {
        return;
    }
    // Searching for new Bluetooth devices only while that list is shown.
    if (m_list == List::AddDevice && m_actions.stopBluetoothSearch)
    {
        m_actions.stopBluetoothSearch();
    }
    m_list = list;
    m_page = 0;
    StartRefreshing();
    ApplyTiles(true);
    ApplyList(true);
}

void ConnectionsSection::OnVisibilityChanged(bool visible)
{
    m_visible = visible;
    if (visible)
    {
        StartRefreshing();
        return;
    }
    if (m_rescan)
    {
        m_rescan.Stop();
    }
    if (m_list == List::AddDevice)
    {
        if (m_actions.stopBluetoothSearch)
        {
            m_actions.stopBluetoothSearch();
        }
        m_list = List::Bluetooth;  // next time, start from the paired devices
    }
    m_page = 0;
    if (m_built)
    {
        // This also stops every "working" pulse: nothing animates while hidden.
        ApplyTiles(false);
        ApplyList(false);
    }
}

void ConnectionsSection::StartRefreshing()
{
    if (!m_visible)
    {
        return;
    }
    if (m_list == List::AddDevice && m_actions.startBluetoothSearch)
    {
        m_actions.startBluetoothSearch();
    }
    if (m_list == List::Wifi)
    {
        // Fresh networks now, then every few seconds while the list is on screen.
        if (m_actions.scanWifi)
        {
            m_actions.scanWifi();
        }
        if (!m_rescan)
        {
            m_rescan = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_rescan.Interval(std::chrono::seconds(10));
            m_rescan.Tick([this](auto&&, auto&&)
            {
                if (m_visible && m_list == List::Wifi && m_actions.scanWifi)
                {
                    m_actions.scanWifi();
                }
            });
        }
        m_rescan.Start();
    }
    else if (m_rescan)
    {
        m_rescan.Stop();
    }
}

void ConnectionsSection::SetRadios(const RadiosState& radios, bool animate)
{
    m_radios = radios;
    if (m_built)
    {
        ApplyTiles(animate);
        ApplyList(animate);
    }
}

void ConnectionsSection::SetWifi(const WifiState& wifi, bool animate)
{
    if (m_wifi.scanning && !wifi.scanning)
    {
        m_wifiScanned = true;
    }
    m_wifi = wifi;
    if (m_built)
    {
        ApplyList(animate);
    }
}

void ConnectionsSection::SetBluetooth(const BluetoothState& bluetooth, bool animate)
{
    m_bluetooth = bluetooth;
    if (m_built)
    {
        ApplyList(animate);
    }
}

void ConnectionsSection::ApplyTiles(bool animate)
{
    auto apply = [&](ToggleTile& tile, const RadioInfo& radio, bool selected)
    {
        tile.SetOn(radio.on, animate);
        tile.SetSubtitle(radio.detail);
        tile.SetBusy(radio.busy && m_visible);  // the pulse only runs on screen
        tile.SetEnabled(radio.available);
        tile.SetSelected(selected, animate);
    };
    apply(*m_wifiTile, m_radios.wifi, m_list == List::Wifi);
    apply(*m_bluetoothTile, m_radios.bluetooth, m_list != List::Wifi);
}

void ConnectionsSection::ApplyList(bool animate)
{
    struct Line
    {
        std::wstring id;
        wchar_t glyph = 0;
        std::wstring title, detail;
        wchar_t trailing = 0;
        bool active = false, busy = false, clickable = true;
    };
    std::vector<Line> lines;
    Line footer;

    if (m_list == List::Wifi)
    {
        if (!m_radios.wifi.on)
        {
            lines.push_back({ L"", Glyph::Wifi, L"Wi-Fi is off", L"Turn it on with the switch above", 0, false, false, false });
        }
        else
        {
            for (const WifiNetwork& network : m_wifi.networks)
            {
                const bool joining = network.ssid == m_wifi.connecting;
                const std::wstring detail = network.connected ? L"Connected"
                                          : joining ? L"Connecting…"
                                          : network.saved ? L"Saved"
                                          : network.secured ? L"Secured" : L"Open";
                lines.push_back({ network.ssid, SignalGlyph(network.bars), network.ssid, detail,
                                  network.secured ? Glyph::Lock : wchar_t{ 0 }, network.connected, joining, !network.connected });
            }
            if (lines.empty())
            {
                lines.push_back({ L"", Glyph::Wifi, m_wifi.scanning ? L"Looking for networks…" : L"No networks found", L"", 0, false,
                                  m_wifi.scanning, false });
            }
            else if (m_wifi.scanning && !m_wifiScanned && lines.size() < kRows)
            {
                // The first scan (only the network you're on is known before it).
                lines.push_back({ L"", Glyph::Wifi, L"Looking for networks…", L"", 0, false, true, false });
            }
        }
        footer = { L"", Glyph::Settings, L"Wi-Fi settings", L"", Glyph::ChevronRight };
    }
    else if (m_list == List::Bluetooth)
    {
        if (!m_radios.bluetooth.on)
        {
            lines.push_back({ L"", Glyph::Bluetooth, L"Bluetooth is off", L"Turn it on with the switch above", 0, false, false, false });
        }
        else
        {
            for (const BluetoothDevice& device : m_bluetooth.paired)
            {
                lines.push_back({ device.id, DeviceGlyph(device.kind), device.name, device.detail, 0, device.connected, device.busy, true });
            }
            if (lines.empty())
            {
                lines.push_back({ L"", Glyph::Bluetooth, L"No paired devices", L"Add one below", 0, false, false, false });
            }
        }
        footer = { L"", Glyph::Add, L"Add a device", L"", Glyph::ChevronRight };
    }
    else
    {
        for (const BluetoothDevice& device : m_bluetooth.nearby)
        {
            lines.push_back({ device.id, DeviceGlyph(device.kind), device.name, device.detail, 0, false, device.busy, !device.busy });
        }
        if (lines.empty())
        {
            lines.push_back({ L"", Glyph::Bluetooth, L"Looking for devices…", L"Put the device in pairing mode", 0, false, true, false });
        }
        footer = { L"", Glyph::Back, L"Back to my devices" };
    }

    // Too many to show at once: the last row pages through them.
    const ListPage page = PageLines(static_cast<int>(lines.size()), kRows, m_page);
    m_page = page.page;
    std::vector<Line> shown(lines.begin() + page.first, lines.begin() + page.first + page.count);
    if (page.more)
    {
        const std::wstring range = std::format(L"{}–{} of {}", page.first + 1, page.first + page.count, lines.size());
        shown.push_back({ L"", Glyph::More, m_list == List::Wifi ? L"More networks" : L"More devices", range, Glyph::ChevronRight });
    }
    m_moreRow = page.more ? static_cast<int>(shown.size()) - 1 : -1;

    for (int i = 0; i < kRows; ++i)
    {
        ListRow& row = *m_rows[i];
        if (i >= static_cast<int>(shown.size()))
        {
            m_rowIds[i].clear();
            row.SetShown(false, animate);
            continue;
        }
        const Line& line = shown[i];
        m_rowIds[i] = line.id;
        row.SetContent(line.glyph, line.title, line.detail, line.trailing, line.active, animate);
        row.SetBusy(line.busy && m_visible);  // the pulse only runs on screen
        row.SetShown(true, animate);
        row.SetInteractive(line.clickable);
    }
    m_footer->SetContent(footer.glyph, footer.title, footer.detail, footer.trailing, false, animate);
}

void ConnectionsSection::OnRowClicked(int row)
{
    if (row == m_moreRow)
    {
        ++m_page;  // PageLines wraps from the last page to the first
        ApplyList(true);
        return;
    }
    const std::wstring& id = m_rowIds[row];
    if (id.empty())
    {
        return;
    }
    switch (m_list)
    {
    case List::Wifi:
        if (m_actions.connectWifi) m_actions.connectWifi(id);
        break;
    case List::Bluetooth:
    {
        // Island can connect headphones and speakers; for other devices
        // (mice, keyboards) Windows manages the connection itself.
        const auto device = std::find_if(m_bluetooth.paired.begin(), m_bluetooth.paired.end(),
            [&](const BluetoothDevice& entry) { return entry.id == id; });
        if (device != m_bluetooth.paired.end() && device->canConnect)
        {
            if (m_actions.toggleBluetoothDevice) m_actions.toggleBluetoothDevice(id);
        }
        else if (m_actions.openSettings)
        {
            m_actions.openSettings(L"ms-settings:bluetooth");
        }
        break;
    }
    case List::AddDevice:
        if (m_actions.pairBluetoothDevice) m_actions.pairBluetoothDevice(id);
        break;
    }
}
