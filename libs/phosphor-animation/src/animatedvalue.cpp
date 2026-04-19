// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// AnimatedValue<T> is a header-only template (see AnimatedValue.h). This
// translation unit exists only to host the library-wide logging category
// so every instantiation shares one `phosphoranimation.animatedvalue`
// rule string, rather than each template instantiation getting its own
// static category.

#include <PhosphorAnimation/AnimatedValue.h>

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcAnimatedValue, "phosphoranimation.animatedvalue")

std::shared_ptr<const Curve> defaultFallbackCurve()
{
    // Meyers singleton: one shared OutCubic default across every
    // AnimatedValue<T> instantiation. Default-constructed Easing is
    // OutCubic per Easing::Easing's defaults — matches Phase 2
    // CurveRegistry::create("")'s fallback shape.
    static const std::shared_ptr<const Curve> sFallback = std::make_shared<const Easing>();
    return sFallback;
}

const void* IMotionClock::steadyClockEpoch()
{
    // Shared sentinel address for every std::chrono::steady_clock-backed
    // IMotionClock. The pointee is never dereferenced; only the pointer
    // identity matters for the rebind-compatibility test in
    // AnimatedValue::rebindClock / AnimationController::advanceAnimations.
    // Exporting this from a single TU guarantees one unique address
    // across the process.
    static const char kSentinel{};
    return &kSentinel;
}

} // namespace PhosphorAnimation
