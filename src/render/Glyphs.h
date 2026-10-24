#pragma once

// Icons from Segoe Fluent Icons, the icon font built into Windows 11 (with
// Segoe MDL2 Assets, its Windows 10 predecessor, as a fallback). Each icon is
// one character of the font.
namespace Glyph
{
    constexpr wchar_t Play = 0xE768;
    constexpr wchar_t Pause = 0xE769;
    constexpr wchar_t Previous = 0xE892;
    constexpr wchar_t Next = 0xE893;
    constexpr wchar_t MusicNote = 0xE8D6;
    constexpr wchar_t Mute = 0xE74F;
    constexpr wchar_t Volume0 = 0xE992;
    constexpr wchar_t Volume1 = 0xE993;
    constexpr wchar_t Volume2 = 0xE994;
    constexpr wchar_t Volume3 = 0xE995;
    constexpr wchar_t Brightness = 0xE706;
    constexpr wchar_t Wifi = 0xE701;     // full signal
    constexpr wchar_t Wifi1 = 0xE872;    // weak signal
    constexpr wchar_t Wifi2 = 0xE873;
    constexpr wchar_t Wifi3 = 0xE874;
    constexpr wchar_t Lock = 0xE72E;
    constexpr wchar_t Bluetooth = 0xE702;
    constexpr wchar_t Display = 0xE7F4;

    // Battery0 + n = n tenths full (0..10); the same for BatteryCharging0.
    constexpr wchar_t Battery0 = 0xEBA0;
    constexpr wchar_t BatteryCharging0 = 0xEBAB;
    constexpr wchar_t Plug = 0xE83E;

    // Kinds of Bluetooth device
    constexpr wchar_t Headphone = 0xE7F6;
    constexpr wchar_t Speakers = 0xE7F5;
    constexpr wchar_t Mouse = 0xE962;
    constexpr wchar_t Keyboard = 0xE765;
    constexpr wchar_t CellPhone = 0xE8EA;
    constexpr wchar_t Laptop = 0xE7F8;
    constexpr wchar_t GameController = 0xE7FC;

    constexpr wchar_t Add = 0xE710;
    constexpr wchar_t Back = 0xE72B;
    constexpr wchar_t ChevronRight = 0xE76C;
    constexpr wchar_t More = 0xE712;
    constexpr wchar_t Settings = 0xE713;
}
