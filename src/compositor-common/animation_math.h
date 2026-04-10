// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "easingcurve.h"

#include <QLineF>
#include <QMarginsF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <optional>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic animation math utilities
 *
 * These functions contain the pure math for snap animation validation
 * and overshoot bounding, extracted from WindowAnimator.
 */
namespace AnimationMath {

/**
 * @brief Validate and create a snap animation if the transition is meaningful.
 *
 * Returns std::nullopt if the animation should be skipped (disabled, degenerate
 * target, distance below threshold, etc.).
 *
 * @param oldPosition   Window position before snap
 * @param oldSize       Window size before snap
 * @param targetGeometry Target geometry to animate to
 * @param config        Animation configuration
 * @return WindowAnimation if valid, std::nullopt if animation should be skipped
 */
std::optional<WindowAnimation> createSnapAnimation(const QPointF& oldPosition, const QSizeF& oldSize,
                                                   const QRect& targetGeometry, const AnimationConfig& config);

/**
 * @brief Compute bounding rect for animation path including overshoot.
 *
 * For easing curves that overshoot (elastic, bounce with high amplitude,
 * bezier with out-of-range control points), samples the curve to find
 * the true extremes.
 *
 * @param startPos      Animation start position
 * @param startSize     Animation start size
 * @param targetGeometry Animation target geometry
 * @param easing        Easing curve
 * @param padding       Extra padding around the animation (shadow/decoration)
 * @return Bounding rect covering the full animation path
 */
QRectF computeOvershootBounds(const QPointF& startPos, const QSizeF& startSize, const QRect& targetGeometry,
                              const EasingCurve& easing, const QMarginsF& padding);

} // namespace AnimationMath
} // namespace PlasmaZones
