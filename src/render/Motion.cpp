#include "render/Motion.h"

#include <algorithm>

namespace wuc = winrt::Windows::UI::Composition;
using winrt::Windows::Foundation::TimeSpan;

namespace
{
    TimeSpan Milliseconds(float ms)
    {
        // TimeSpan counts in units of 100 nanoseconds.
        return TimeSpan{ static_cast<int64_t>(std::max(ms, 1.0f) * 10'000.0f) };
    }

    template <typename Animation>
    void Configure(Animation& animation, const Spring& spring)
    {
        animation.DampingRatio(std::clamp(spring.dampingRatio, 0.05f, 2.0f));
        animation.Period(Milliseconds(spring.periodMs));
    }

    wuc::CubicBezierEasingFunction EaseOut(const wuc::Compositor& compositor)
    {
        // Fast start, gentle stop: the curve Windows 11 itself uses for fades.
        return compositor.CreateCubicBezierEasingFunction({ 0.1f, 0.9f }, { 0.2f, 1.0f });
    }
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float value, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringScalarAnimation();
    Configure(animation, spring);
    animation.FinalValue(value);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float2 value, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringVector2Animation();
    Configure(animation, spring);
    animation.FinalValue(value);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float3 value, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringVector3Animation();
    Configure(animation, spring);
    animation.FinalValue(value);
    target.StartAnimation(property, animation);
}

void Motion::EaseTo(const CompositionObject& target, const wchar_t* property, float value, int durationMs, int delayMs)
{
    const auto compositor = target.Compositor();
    auto animation = compositor.CreateScalarKeyFrameAnimation();
    // With only a final key frame, the animation starts from the current value.
    animation.InsertKeyFrame(1.0f, value, EaseOut(compositor));
    animation.Duration(Milliseconds(static_cast<float>(durationMs)));
    if (delayMs > 0)
    {
        animation.DelayTime(Milliseconds(static_cast<float>(delayMs)));
    }
    target.StartAnimation(property, animation);
}

void Motion::Pulse(const CompositionObject& target, const wchar_t* property, float high, float low, int periodMs)
{
    const auto compositor = target.Compositor();
    auto animation = compositor.CreateScalarKeyFrameAnimation();
    const auto ease = compositor.CreateCubicBezierEasingFunction({ 0.45f, 0.0f }, { 0.55f, 1.0f });
    animation.InsertKeyFrame(0.0f, high);
    animation.InsertKeyFrame(0.5f, low, ease);
    animation.InsertKeyFrame(1.0f, high, ease);
    animation.Duration(Milliseconds(static_cast<float>(periodMs)));
    animation.IterationBehavior(wuc::AnimationIterationBehavior::Forever);
    target.StartAnimation(property, animation);
}
