#include "render/Motion.h"

#include <algorithm>
#include <cmath>

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
    void Configure(Animation& animation, const Spring& spring, int delayMs)
    {
        animation.DampingRatio(std::clamp(spring.dampingRatio, 0.05f, 2.0f));
        animation.Period(Milliseconds(spring.periodMs));
        if (delayMs > 0)
        {
            animation.DelayTime(Milliseconds(static_cast<float>(delayMs)));
        }
    }

    wuc::CubicBezierEasingFunction EaseOut(const wuc::Compositor& compositor)
    {
        // Fast start, gentle stop: the curve Windows 11 itself uses for fades.
        return compositor.CreateCubicBezierEasingFunction({ 0.1f, 0.9f }, { 0.2f, 1.0f });
    }
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float value, const Spring& spring, int delayMs)
{
    auto animation = target.Compositor().CreateSpringScalarAnimation();
    Configure(animation, spring, delayMs);
    animation.FinalValue(value);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float2 value, const Spring& spring, int delayMs)
{
    auto animation = target.Compositor().CreateSpringVector2Animation();
    Configure(animation, spring, delayMs);
    animation.FinalValue(value);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float3 value, const Spring& spring, int delayMs)
{
    auto animation = target.Compositor().CreateSpringVector3Animation();
    Configure(animation, spring, delayMs);
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

namespace
{
    template <typename Animation, typename Value>
    void Ease(const wuc::CompositionObject& target, const wchar_t* property, Animation animation, Value value, int durationMs, int delayMs)
    {
        const auto compositor = target.Compositor();
        animation.InsertKeyFrame(1.0f, value, EaseOut(compositor));
        animation.Duration(Milliseconds(static_cast<float>(durationMs)));
        if (delayMs > 0)
        {
            animation.DelayTime(Milliseconds(static_cast<float>(delayMs)));
        }
        target.StartAnimation(property, animation);
    }
}

void Motion::EaseTo(const CompositionObject& target, const wchar_t* property, float2 value, int durationMs, int delayMs)
{
    Ease(target, property, target.Compositor().CreateVector2KeyFrameAnimation(), value, durationMs, delayMs);
}

void Motion::EaseTo(const CompositionObject& target, const wchar_t* property, float3 value, int durationMs, int delayMs)
{
    Ease(target, property, target.Compositor().CreateVector3KeyFrameAnimation(), value, durationMs, delayMs);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float value, float velocity, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringScalarAnimation();
    Configure(animation, spring, 0);
    animation.FinalValue(value);
    animation.InitialVelocity(velocity);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float2 value, float2 velocity, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringVector2Animation();
    Configure(animation, spring, 0);
    animation.FinalValue(value);
    animation.InitialVelocity(velocity);
    target.StartAnimation(property, animation);
}

void Motion::SpringTo(const CompositionObject& target, const wchar_t* property, float3 value, float3 velocity, const Spring& spring)
{
    auto animation = target.Compositor().CreateSpringVector3Animation();
    Configure(animation, spring, 0);
    animation.FinalValue(value);
    animation.InitialVelocity(velocity);
    target.StartAnimation(property, animation);
}

Motion::SpringPoint Motion::SpringAt(const Spring& spring, float from, float velocity, float to, float seconds)
{
    // x(t) - to = e^(-z w t) (A cos(wd t) + B sin(wd t)), with w the spring's
    // own frequency (one swing per period), z its damping and wd = w sqrt(1 - z^2).
    const double w = 2.0 * 3.14159265358979 / (std::max(1.0f, spring.periodMs) / 1000.0);
    const double z = std::clamp(spring.dampingRatio, 0.05f, 2.0f);
    const double t = std::max(0.0f, seconds);
    const double a = from - to;
    double x = 0.0, v = 0.0;
    if (z < 0.999)
    {
        const double wd = w * std::sqrt(1.0 - z * z);
        const double b = (velocity + z * w * a) / wd;
        const double decay = std::exp(-z * w * t);
        const double c = std::cos(wd * t), s = std::sin(wd * t);
        x = decay * (a * c + b * s);
        v = decay * ((-z * w) * (a * c + b * s) + (-a * wd * s + b * wd * c));
    }
    else
    {
        // Critically damped (or more): no swing; close enough for our springs.
        const double b = velocity + w * a;
        const double decay = std::exp(-w * t);
        x = decay * (a + b * t);
        v = decay * (b - w * (a + b * t));
    }
    return { static_cast<float>(to + x), static_cast<float>(v) };
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
