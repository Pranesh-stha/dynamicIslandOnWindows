#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>

// A spring, described by the same two numbers the Windows compositor uses:
//   dampingRatio: 1 = glides in with no bounce, lower = a little overshoot.
//   periodMs:     smaller = faster.
struct Spring
{
    float dampingRatio = 0.8f;
    float periodMs = 50.0f;
};

// The shared "motion tokens": every animation in Island takes its timing from
// here, so all controls feel like one family. They come from settings.json,
// so the feel can be tuned without recompiling.
struct MotionSettings
{
    Spring morph{ 0.78f, 65.0f };    // the island changing shape
    Spring press{ 0.55f, 35.0f };    // a control shrinking on press and springing back
    Spring toggle{ 0.72f, 45.0f };   // toggle knobs and the segmented selection
    Spring slider{ 0.9f, 45.0f };    // slider glides (track clicks, outside changes)
    int hoverFadeMs = 120;           // hover highlights fading in and out
    int contentFadeOutMs = 90;       // old content disappearing during a morph
    int contentFadeInMs = 200;       // new content appearing during a morph
    int contentFadeInDelayMs = 80;   // ...starting this long into the morph
    int hoverIntentMs = 150;         // the mouse must rest this long before Peek opens
    int leaveDelayMs = 300;          // Peek closes this long after the mouse leaves
    int expandedLeaveDelayMs = 500;  // the same for the full control center
};

// Helpers that start compositor animations. They all begin from the
// property's current on-screen value, even mid-animation, so changing your
// mind halfway never makes anything jump.
namespace Motion
{
    using winrt::Windows::Foundation::Numerics::float2;
    using winrt::Windows::Foundation::Numerics::float3;
    using winrt::Windows::UI::Composition::CompositionObject;

    void SpringTo(const CompositionObject& target, const wchar_t* property, float value, const Spring& spring, int delayMs = 0);
    void SpringTo(const CompositionObject& target, const wchar_t* property, float2 value, const Spring& spring, int delayMs = 0);
    void SpringTo(const CompositionObject& target, const wchar_t* property, float3 value, const Spring& spring, int delayMs = 0);

    // A smooth ease-out over a fixed time. Used for fades.
    void EaseTo(const CompositionObject& target, const wchar_t* property, float value, int durationMs, int delayMs = 0);
    void EaseTo(const CompositionObject& target, const wchar_t* property, float2 value, int durationMs, int delayMs = 0);
    void EaseTo(const CompositionObject& target, const wchar_t* property, float3 value, int durationMs, int delayMs = 0);

    // A spring started with a speed (units per second) instead of from rest:
    // how a reversed morph keeps going smoothly instead of stopping first.
    void SpringTo(const CompositionObject& target, const wchar_t* property, float value, float velocity, const Spring& spring);
    void SpringTo(const CompositionObject& target, const wchar_t* property, float2 value, float2 velocity, const Spring& spring);
    void SpringTo(const CompositionObject& target, const wchar_t* property, float3 value, float3 velocity, const Spring& spring);

    // Where a spring is, and how fast it moves (units per second), `seconds`
    // after it set off from `from` at `velocity` towards `to` (a damped
    // oscillator, as the compositor's springs are). Pure maths, unit-tested.
    struct SpringPoint
    {
        float value;
        float velocity;
    };
    SpringPoint SpringAt(const Spring& spring, float from, float velocity, float to, float seconds);

    // A repeating pulse between the current value and `low` (the "working"
    // shimmer). Stop it with target.StopAnimation(property).
    void Pulse(const CompositionObject& target, const wchar_t* property, float high, float low, int periodMs);
}
