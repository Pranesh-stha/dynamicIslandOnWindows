#include "views/sections/ConnectionsSection.h"

#include <algorithm>
#include <chrono>

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

int ScrollFirst(int count, int rows, int first)
{
    return std::clamp(first, 0, std::max(0, count - rows));
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

    // The list: one row per line, in a window four rows tall that the mouse
    // wheel scrolls a row at a time. When it scrolls, the rows move to their
    // new places at once (so a click lands on the row you see once it
    // settles) while their picture slides there with a spring. One hover
    // highlight glides between the rows. The footer stays put underneath.
    m_controls = &controls;
    m_rowHeight = ui.Px(36.0f);
    const float rowGap = ui.Px(2.0f);
    m_rowStep = m_rowHeight + rowGap;
    m_listX = x;
    m_listTop = y;
    m_listWidth = width;
    m_listHeight = m_rowStep * kRows - rowGap;
    m_viewport = ui.compositor.CreateContainerVisual();
    m_viewport.Size({ x + width, y + m_listHeight });
    m_viewport.Clip(ui.compositor.CreateInsetClip(x, y, 0.0f, 0.0f));  // only the list's own window shows
    layer.Children().InsertAtTop(m_viewport);
    m_content = ui.compositor.CreateContainerVisual();
    m_viewport.Children().InsertAtTop(m_content);
    m_spotlight = std::make_unique<Spotlight>(ui);
    m_content.Children().InsertAtTop(m_spotlight->Visual());
    m_rows.clear();
    m_rowIds.clear();
    m_rowClickable.clear();
    m_lineCount = 0;

    // A thin bar at the right edge: where you are in a long list.
    const float thumbWidth = ui.Px(3.0f);
    m_thumb = RoundedRect(ui, thumbWidth, m_listHeight, thumbWidth / 2.0f, White(0.3f));
    m_thumb.visual.Opacity(0.0f);
    m_thumb.visual.Offset({ x + width - thumbWidth, y, 0.0f });
    m_viewport.Children().InsertAtTop(m_thumb.visual);

    m_wheelArea = std::make_unique<WheelArea>(ui);
    m_wheelArea->SetBounds(x, y, width, m_listHeight);
    m_wheelArea->onWheel = [this](float notches) { return OnWheel(notches); };
    controls.Add(*m_wheelArea);
    ResetScroll();
    y += m_listHeight + rowGap;

    m_footer = std::make_unique<ListRow>(ui);  // its own highlight: the list's is clipped to the list
    m_footer->SetBounds(x, y, width, m_rowHeight);
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
    y += m_rowHeight;

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
    ResetScroll();
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
    if (m_spotlight)
    {
        m_spotlight->Hide();
    }
    if (m_list == List::AddDevice)
    {
        if (m_actions.stopBluetoothSearch)
        {
            m_actions.stopBluetoothSearch();
        }
        m_list = List::Bluetooth;  // next time, start from the paired devices
    }
    if (m_built)
    {
        ResetScroll();  // next time, from the top
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

    // Longer than the four rows: the list scrolls (rows below the window are
    // still filled in, so they're ready when they slide into view).
    if (lines.size() > static_cast<size_t>(kMaxLines))
    {
        lines.resize(kMaxLines);
    }
    m_lineCount = static_cast<int>(lines.size());
    for (int i = 0; i < m_lineCount; ++i)
    {
        const Line& line = lines[i];
        ListRow& row = Row(i);
        m_rowIds[i] = line.id;
        m_rowClickable[i] = line.clickable;
        row.SetContent(line.glyph, line.title, line.detail, line.trailing, line.active, animate);
        row.SetBusy(line.busy && m_visible);  // the pulse only runs on screen
        row.SetShown(true, animate);
    }
    for (int i = m_lineCount; i < static_cast<int>(m_rows.size()); ++i)
    {
        m_rowIds[i].clear();
        m_rows[i]->SetShown(false, animate);
    }
    ScrollTo(m_scroll, animate);  // the list may have got shorter
    PlaceRows(animate);
    m_footer->SetContent(footer.glyph, footer.title, footer.detail, footer.trailing, false, animate);
}

ListRow& ConnectionsSection::Row(int index)
{
    while (static_cast<int>(m_rows.size()) <= index)
    {
        const int i = static_cast<int>(m_rows.size());
        auto row = std::make_unique<ListRow>(m_ui);
        row->SetSpotlight(m_spotlight.get());
        row->onClick = [this, i] { OnRowClicked(i); };
        row->onRightClick = [this]
        {
            if (m_actions.openSettings)
            {
                m_actions.openSettings(m_list == List::Wifi ? L"ms-settings:network-wifi" : L"ms-settings:bluetooth");
            }
        };
        row->SetShown(false, false);
        m_content.Children().InsertAtTop(row->Visual());
        m_controls->Add(*row);
        m_rows.push_back(std::move(row));
        m_rowIds.emplace_back();
        m_rowClickable.push_back(false);
    }
    return *m_rows[index];
}

bool ConnectionsSection::OnWheel(float notches)
{
    if (m_lineCount <= kRows)
    {
        m_wheel = 0.0f;
        return false;  // nothing to scroll
    }
    // A row per notch; a touchpad's small steps add up to notches.
    m_wheel += notches;
    const int rows = static_cast<int>(m_wheel);  // whole notches, towards zero
    m_wheel -= static_cast<float>(rows);
    if (rows != 0)
    {
        ScrollTo(m_scroll - rows, true);  // wheel up: towards the top of the list
    }
    return true;
}

void ConnectionsSection::ScrollTo(int first, bool animate)
{
    first = ScrollFirst(m_lineCount, kRows, first);
    const int moved = first - m_scroll;  // rows moved up by this many
    if (moved == 0)
    {
        return;
    }
    m_scroll = first;
    PlaceRows(animate);
    if (!animate || !m_visible)
    {
        StopSlide();
        return;
    }
    // The rows have jumped to their new places. Their picture starts where it
    // was (shifted back by the same distance) and springs to rest, keeping
    // its speed if it was still sliding from the last notch.
    const Motion::SpringPoint now = Slide();
    m_slideFrom = now.value + static_cast<float>(moved) * m_rowStep;
    m_slideVelocity = now.velocity;
    m_slideStart = GetTickCount64();
    m_content.StopAnimation(L"Offset");
    m_content.Offset({ 0.0f, m_slideFrom, 0.0f });
    Motion::SpringTo(m_content, L"Offset", Motion::float3{ 0.0f, 0.0f, 0.0f }, Motion::float3{ 0.0f, m_slideVelocity, 0.0f },
                     m_ui.motion->slider);
}

void ConnectionsSection::ResetScroll()
{
    m_scroll = 0;
    m_wheel = 0.0f;
    StopSlide();
}

void ConnectionsSection::StopSlide()
{
    m_slideFrom = 0.0f;
    m_slideVelocity = 0.0f;
    if (m_content)
    {
        m_content.StopAnimation(L"Offset");
        m_content.Offset({ 0.0f, 0.0f, 0.0f });
    }
}

Motion::SpringPoint ConnectionsSection::Slide() const
{
    if (m_slideFrom == 0.0f && m_slideVelocity == 0.0f)
    {
        return { 0.0f, 0.0f };
    }
    const float seconds = static_cast<float>(GetTickCount64() - m_slideStart) / 1000.0f;
    return Motion::SpringAt(m_ui.motion->slider, m_slideFrom, m_slideVelocity, 0.0f, seconds);
}

void ConnectionsSection::PlaceRows(bool animate)
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        ListRow& row = *m_rows[i];
        row.SetBounds(m_listX, m_listTop + static_cast<float>(i - m_scroll) * m_rowStep, m_listWidth, m_rowHeight);
        // Rows scrolled out of the window are clipped away and mustn't take the mouse.
        const bool inWindow = i >= m_scroll && i < m_scroll + kRows;
        row.SetInteractive(i < m_lineCount && inWindow && m_rowClickable[i]);
    }

    // The scroll bar: as tall as the share of the list on screen.
    const bool scrolls = m_lineCount > kRows;
    const float thumbWidth = m_thumb.visual.Size().x;
    const float thumbHeight = scrolls ? std::max(m_ui.Px(16.0f), std::round(m_listHeight * kRows / m_lineCount)) : m_listHeight;
    const float thumbY = m_listTop + (scrolls ? std::round((m_listHeight - thumbHeight) * m_scroll / (m_lineCount - kRows)) : 0.0f);
    m_thumb.Resize(thumbWidth, thumbHeight, thumbWidth / 2.0f);
    const Motion::float3 thumbAt{ m_listX + m_listWidth - thumbWidth, thumbY, 0.0f };
    if (animate && m_visible)
    {
        Motion::SpringTo(m_thumb.visual, L"Offset", thumbAt, m_ui.motion->slider);
        Motion::EaseTo(m_thumb.visual, L"Opacity", scrolls ? 1.0f : 0.0f, 160);
    }
    else
    {
        m_thumb.visual.StopAnimation(L"Offset");
        m_thumb.visual.StopAnimation(L"Opacity");
        m_thumb.visual.Offset(thumbAt);
        m_thumb.visual.Opacity(scrolls ? 1.0f : 0.0f);
    }
}

void ConnectionsSection::OnRowClicked(int row)
{
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
