// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QString>

#include <cmath>

namespace PhosphorLayout {

/**
 * @brief Screen aspect-ratio classification.
 *
 * Used to tag layouts with their intended monitor type and to classify
 * physical screens at runtime so the layout picker can recommend matching
 * layouts.  The same enum is the type of @c LayoutPreview::aspectRatioClass
 * so consumers can group/filter previews uniformly regardless of which
 * provider produced them.
 */
enum class AspectRatioClass {
    Any = 0, ///< Suitable for all aspect ratios (default)
    Standard = 1, ///< ~16:10 to ~16:9 (1.5 - 1.9)
    Ultrawide = 2, ///< ~21:9 (1.9 - 2.8)
    SuperUltrawide = 3, ///< ~32:9 (2.8+)
    Portrait = 4 ///< Rotated/vertical monitors (< 1.0)
};

/**
 * @brief Screen classification thresholds and utilities.
 *
 * Free helpers separated from the enum so consumers can pull just the
 * enum without dragging the threshold constants into namespace pollution.
 */
namespace ScreenClassification {

// Aspect ratio boundary thresholds (width / height).
constexpr qreal PortraitMax = 1.0; ///< AR < 1.0 → portrait
constexpr qreal UltrawideMin = 1.9; ///< AR ∈ [UltrawideMin, SuperUltrawideMin) → ultrawide
constexpr qreal SuperUltrawideMin = 2.8; ///< AR >= 2.8 → super-ultrawide

inline AspectRatioClass classify(qreal aspectRatio)
{
    // NaN never compares true under <, which would silently fall through every
    // branch and land at SuperUltrawide. Treat non-finite as "no classification"
    // so callers that compute aspect ratios from suspect inputs get a stable
    // sentinel rather than a wildly wrong category.
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0) {
        return AspectRatioClass::Any;
    }
    if (aspectRatio < PortraitMax) {
        return AspectRatioClass::Portrait;
    }
    if (aspectRatio < UltrawideMin) {
        return AspectRatioClass::Standard;
    }
    if (aspectRatio < SuperUltrawideMin) {
        return AspectRatioClass::Ultrawide;
    }
    return AspectRatioClass::SuperUltrawide;
}

inline AspectRatioClass classify(int width, int height)
{
    if (height <= 0 || width <= 0) {
        return AspectRatioClass::Any;
    }
    return classify(static_cast<qreal>(width) / height);
}

inline QString toString(AspectRatioClass cls)
{
    switch (cls) {
    case AspectRatioClass::Any:
        return QStringLiteral("any");
    case AspectRatioClass::Standard:
        return QStringLiteral("standard");
    case AspectRatioClass::Ultrawide:
        return QStringLiteral("ultrawide");
    case AspectRatioClass::SuperUltrawide:
        return QStringLiteral("super-ultrawide");
    case AspectRatioClass::Portrait:
        return QStringLiteral("portrait");
    }
    return QStringLiteral("any");
}

inline AspectRatioClass fromString(const QString& str)
{
    if (str == QLatin1String("standard")) {
        return AspectRatioClass::Standard;
    }
    if (str == QLatin1String("ultrawide")) {
        return AspectRatioClass::Ultrawide;
    }
    if (str == QLatin1String("super-ultrawide")) {
        return AspectRatioClass::SuperUltrawide;
    }
    if (str == QLatin1String("portrait")) {
        return AspectRatioClass::Portrait;
    }
    return AspectRatioClass::Any;
}

/**
 * @brief Representative aspect ratio for a class.
 * @param cls       The aspect ratio class.
 * @param fallback  Value to return for AspectRatioClass::Any (default 16:9).
 */
inline qreal aspectRatioForClass(AspectRatioClass cls, qreal fallback = 16.0 / 9.0)
{
    switch (cls) {
    case AspectRatioClass::Standard:
        return 16.0 / 9.0;
    case AspectRatioClass::Ultrawide:
        return 21.0 / 9.0;
    case AspectRatioClass::SuperUltrawide:
        return 32.0 / 9.0;
    case AspectRatioClass::Portrait:
        return 9.0 / 16.0;
    case AspectRatioClass::Any:
        return fallback;
    }
    // Unreachable under @c -Wswitch (every enumerator handled above). Kept
    // to satisfy compilers that still warn about control reaching the end
    // of a non-void function when the enum-covering switch exits the path.
    return fallback;
}

/**
 * @brief Check if a layout's aspect ratio class matches the given screen class.
 *
 * A layout with @c AspectRatioClass::Any matches every screen.
 * Otherwise, exact match is required unless the layout specifies
 * explicit min/max aspect ratio bounds (which take precedence at the
 * layout consumer's level — this helper just does the class match).
 */
inline bool matches(AspectRatioClass layoutClass, AspectRatioClass screenClass)
{
    if (layoutClass == AspectRatioClass::Any) {
        return true;
    }
    return layoutClass == screenClass;
}

} // namespace ScreenClassification

} // namespace PhosphorLayout
