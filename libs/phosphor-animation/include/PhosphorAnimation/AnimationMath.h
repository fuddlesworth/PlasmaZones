// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/WindowMotion.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QLineF>
#include <QMarginsF>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>

#include <optional>

namespace PhosphorAnimation {

/**
 * @brief Pure math helpers for snap animation validation + repaint bounds.
 *
 * These functions exist outside @ref WindowMotion to keep the state
 * struct trivially copyable and to let callers invoke them without
 * constructing an animation (e.g., precomputing repaint regions before
 * the WindowMotion has latched its `startTime`).
 */
namespace AnimationMath {

/**
 * @brief Build a @ref WindowMotion if the transition is worth animating.
 *
 * Returns `std::nullopt` when:
 *   - the target geometry is degenerate (zero or negative size),
 *   - the position change is below `max(1, minDistance)` pixels AND the
 *     size isn't changing (the animation wouldn't be visible).
 *
 * @note `minDistance == 0` is treated as "1 pixel" — a 0-pixel move with
 * no size change is a no-op and always gets skipped. Callers that want
 * every movement animated should request explicit motion via a different
 * entry point rather than rely on `minDistance = 0`.
 *
 * Otherwise returns a populated WindowMotion with `startTime` still
 * pending — the first `updateProgress()` call latches it.
 *
 * The "should I animate at all?" gate (e.g., a global enable flag) lives
 * with the caller — this function never refuses on policy grounds, only
 * on geometry.
 */
PHOSPHORANIMATION_EXPORT std::optional<WindowMotion> createSnapMotion(const QPointF& oldPosition, const QSizeF& oldSize,
                                                                      const QRect& targetGeometry, qreal duration,
                                                                      const Easing& easing, int minDistance);

/**
 * @brief Bounding rect covering the full animation path including
 * overshoot.
 *
 * Elastic and bounce curves — and out-of-range bezier y controls — can
 * send the visual position outside the start/target union during the
 * middle of the animation. This function samples the curve when
 * necessary and returns a union rect that's guaranteed to contain every
 * frame's damage region, so callers can invalidate a single rectangle
 * per animation instead of guessing per frame.
 *
 * @p padding  Extra margin added around each sample (shadow / decoration
 *             bleed). Same value is applied on all four edges of each
 *             sampled rect before unioning.
 */
PHOSPHORANIMATION_EXPORT QRectF repaintBounds(const QPointF& startPos, const QSizeF& startSize,
                                              const QRect& targetGeometry, const Easing& easing,
                                              const QMarginsF& padding);

} // namespace AnimationMath

} // namespace PhosphorAnimation
