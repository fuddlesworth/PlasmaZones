// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// AnimatedValue<T> is a header-only template (see AnimatedValue.h). This
// translation unit exists only to host the library-wide logging category
// so every instantiation shares one `phosphoranimation.animatedvalue`
// rule string, rather than each template instantiation getting its own
// static category.

#include <PhosphorAnimation/AnimatedValue.h>

#include <PhosphorAnimation/Easing.h>

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

} // namespace PhosphorAnimation
