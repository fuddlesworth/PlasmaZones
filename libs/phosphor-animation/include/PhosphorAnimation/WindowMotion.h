// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QPointF>
#include <QRect>
#include <QSizeF>
#include <QtGlobal>

#include <chrono>
#include <memory>

namespace PhosphorAnimation {

/**
 * @brief Per-window snap-animation state (translate + scale).
 *
 * Stores start position / size + target geometry + the easing curve in
 * use, plus cached progress refreshed once per frame. Consumed by
 * compositor-side runners that translate a window's visual position and
 * scale across frames — today the KWin effect's `WindowAnimator`, later
 * any compositor plugin via the Phase 2 `AnimationController` base.
 *
 * ## Progress semantics
 *
 * `startTime < 0` means pending — latched to the first `presentTime`
 * fed into `updateProgress()`. This guarantees the animation starts at
 * t = 0 on its first paint, not whenever the caller happened to
 * populate the struct.
 *
 * Progress is cached once per frame via `updateProgress(presentTime)`
 * so `currentVisualPosition()` + `currentVisualSize()` return
 * consistent values within a paint cycle (otherwise position and size
 * could drift by one sample on elastic curves).
 *
 * ## Overshoot
 *
 * Progress is usually in [0, 1], but elastic and bounce curves may
 * overshoot by design (e.g., 1.05 or -0.02). Consumers should NOT
 * clamp `progress()` — the overshoot is what makes the motion feel
 * alive. `AnimationMath::repaintBounds()` accounts for this when
 * sizing repaint regions.
 *
 * ## Curve type
 *
 * `curve` is `shared_ptr<const Curve>` so any registered curve subclass
 * (Easing, Spring, third-party) drives window motion via polymorphic
 * `evaluate()`. A null curve is interpreted as linear progression — the
 * cached progress simply equals normalized time `t`.
 */
struct PHOSPHORANIMATION_EXPORT WindowMotion
{
    QPointF startPosition; ///< Visual top-left before snap
    QSizeF startSize; ///< Visual size before snap
    QRect targetGeometry; ///< Final geometry (duplicate detection)
    std::chrono::milliseconds startTime{-1}; ///< presentTime at start (-1 = pending)
    qreal duration = 150.0; ///< Animation length in milliseconds
    std::shared_ptr<const Curve> curve; ///< Polymorphic curve evaluated per frame; null = linear
    qreal cachedProgress = 0.0; ///< Eased progress (per-frame snapshot)

    /// True once @ref startTime has been latched (first paint seen).
    bool isValid() const
    {
        return startTime.count() >= 0;
    }

    /**
     * @brief Latch start time and refresh cached progress.
     *
     * On the first call, `startTime` is latched to @p presentTime so
     * subsequent progress is measured relative to the actual first
     * paint. On later calls, re-evaluates the curve at
     * (presentTime - startTime) / duration and updates
     * @ref cachedProgress.
     *
     * Call exactly once per paint cycle before reading
     * currentVisualPosition / currentVisualSize.
     */
    void updateProgress(std::chrono::milliseconds presentTime)
    {
        if (startTime.count() < 0) {
            // First frame — latch startTime. Zero-duration completes
            // immediately (no elapsed time to measure); otherwise start
            // at t=0 so the first paint renders the start geometry.
            startTime = presentTime;
            cachedProgress = (duration <= 0.0) ? 1.0 : 0.0;
            return;
        }
        if (duration <= 0.0) {
            cachedProgress = 1.0;
            return;
        }
        const qreal elapsed = qreal((presentTime - startTime).count());
        const qreal t = qMin(1.0, elapsed / duration);
        cachedProgress = curve ? curve->evaluate(t) : t;
    }

    /// Eased progress cached by updateProgress(). See Overshoot above.
    qreal progress() const
    {
        return cachedProgress;
    }

    /// True once elapsed ≥ duration. Independent of cachedProgress so
    /// it remains correct even if updateProgress() hasn't been called
    /// this frame.
    bool isComplete(std::chrono::milliseconds presentTime) const
    {
        if (startTime.count() < 0 || duration <= 0.0) {
            return true;
        }
        return qreal((presentTime - startTime).count()) >= duration;
    }

    /// Absolute visual top-left position at the cached progress.
    QPointF currentVisualPosition() const
    {
        const qreal p = cachedProgress;
        const qreal tx = targetGeometry.x();
        const qreal ty = targetGeometry.y();
        return QPointF(startPosition.x() + (tx - startPosition.x()) * p,
                       startPosition.y() + (ty - startPosition.y()) * p);
    }

    /// Interpolated visual size at the cached progress.
    QSizeF currentVisualSize() const
    {
        const qreal p = cachedProgress;
        const qreal tw = targetGeometry.width();
        const qreal th = targetGeometry.height();
        return QSizeF(startSize.width() + (tw - startSize.width()) * p,
                      startSize.height() + (th - startSize.height()) * p);
    }

    /// True if start and target sizes differ by more than 1 pixel.
    /// Used by AnimationMath to decide whether the animation is worth
    /// running (pure position changes under minDistance are skipped).
    bool hasScaleChange() const
    {
        return qAbs(startSize.width() - targetGeometry.width()) > 1.0
            || qAbs(startSize.height() - targetGeometry.height()) > 1.0;
    }
};

} // namespace PhosphorAnimation
