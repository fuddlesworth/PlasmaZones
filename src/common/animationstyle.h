// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QLatin1String>

namespace PlasmaZones {

/**
 * @brief Visual animation style for transitions
 *
 * Shared between src/core (daemon) and kwin-effect (compositor plugin).
 *
 * Styles 0–5 are window-geometry styles (compositor-side).
 * Styles 6–8 are overlay-UI styles (QML daemon-side).
 * AnimationStyle::None and Custom apply to both domains.
 */
enum class AnimationStyle : int {
    // ── Window geometry styles (compositor-side) ──
    Morph = 0, ///< Geometry interpolation only (default for window events)
    Slide = 1, ///< Translate window texture by direction vector
    Popin = 2, ///< Scale up from center + opacity fade
    SlideFade = 3, ///< Partial translate + alpha blend
    None = 4, ///< Instant, no animation
    Custom = 5, ///< User-provided GLSL fragment shader

    // ── Overlay UI styles (QML daemon-side) ──
    FadeIn = 6, ///< Opacity 0→1 (default for overlay events)
    SlideUp = 7, ///< Translate upward + opacity fade
    ScaleIn = 8 ///< Scale from small + opacity fade (popup/OSD default)
};

/**
 * @brief Animation style domain classification
 *
 * Returns "window" for styles applicable to window geometry animations,
 * "overlay" for styles applicable to overlay UI animations,
 * or "both" for styles usable in either domain.
 */
inline QString animationStyleDomain(AnimationStyle style)
{
    switch (style) {
    case AnimationStyle::Morph:
    case AnimationStyle::Slide:
    case AnimationStyle::Popin:
    case AnimationStyle::SlideFade:
        return QStringLiteral("window");
    case AnimationStyle::FadeIn:
    case AnimationStyle::SlideUp:
    case AnimationStyle::ScaleIn:
        return QStringLiteral("overlay");
    case AnimationStyle::None:
    case AnimationStyle::Custom:
        return QStringLiteral("both");
    }
    return QStringLiteral("both");
}

/**
 * @brief Timing mode for animation profiles
 *
 * Distinguishes easing-curve-based timing from spring-physics timing.
 */
enum class TimingMode : int {
    Easing = 0, ///< Duration-based with easing curve
    Spring = 1 ///< Physics-based damped harmonic oscillator
};

inline QString animationStyleToString(AnimationStyle style)
{
    switch (style) {
    case AnimationStyle::Morph:
        return QStringLiteral("morph");
    case AnimationStyle::Slide:
        return QStringLiteral("slide");
    case AnimationStyle::Popin:
        return QStringLiteral("popin");
    case AnimationStyle::SlideFade:
        return QStringLiteral("slidefade");
    case AnimationStyle::None:
        return QStringLiteral("none");
    case AnimationStyle::Custom:
        return QStringLiteral("custom");
    case AnimationStyle::FadeIn:
        return QStringLiteral("fadein");
    case AnimationStyle::SlideUp:
        return QStringLiteral("slideup");
    case AnimationStyle::ScaleIn:
        return QStringLiteral("scalein");
    }
    return QStringLiteral("morph");
}

inline AnimationStyle animationStyleFromString(const QString& str)
{
    if (str == QLatin1String("slide"))
        return AnimationStyle::Slide;
    if (str == QLatin1String("popin"))
        return AnimationStyle::Popin;
    if (str == QLatin1String("slidefade"))
        return AnimationStyle::SlideFade;
    if (str == QLatin1String("none"))
        return AnimationStyle::None;
    if (str == QLatin1String("custom"))
        return AnimationStyle::Custom;
    if (str == QLatin1String("fadein"))
        return AnimationStyle::FadeIn;
    if (str == QLatin1String("slideup"))
        return AnimationStyle::SlideUp;
    if (str == QLatin1String("scalein"))
        return AnimationStyle::ScaleIn;
    return AnimationStyle::Morph;
}

} // namespace PlasmaZones
