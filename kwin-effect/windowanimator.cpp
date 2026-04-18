// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    const PhosphorAnimation::WindowMotion* motion = motionFor(window);
    if (!motion || !motion->isValid()) {
        return;
    }

    // Translate: compute the desired visual position and offset from
    // the actual frameGeometry. moveResize() was called once to set the
    // final geometry, so frameGeometry should be at the target. The
    // offset shrinks to zero as the animation completes.
    const QPointF desiredPos = motion->currentVisualPosition();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);

    // Scale: smoothly morph from old size to target size.
    // visual_size = frameGeometry.size * scale = desiredSize.
    // Scale converges to 1.0 at t=1, so the final state uses the
    // natural buffer with no transform applied.
    if (motion->hasScaleChange()) {
        const QSizeF desiredSize = motion->currentVisualSize();
        const QSizeF actualSize = window->frameGeometry().size();
        constexpr qreal MinDim = 1.0;
        // [0.01, 100] is wide enough that a 4K window snapping to a
        // 100 px thumbnail (40× scale) renders without distortion. The
        // qMax(actualSize, MinDim) above already prevents division
        // explosion, so this clamp only catches genuinely degenerate
        // input — keep it generous.
        const qreal sx = qBound(0.01, desiredSize.width() / qMax(actualSize.width(), MinDim), 100.0);
        const qreal sy = qBound(0.01, desiredSize.height() / qMax(actualSize.height(), MinDim), 100.0);
        data.setXScale(data.xScale() * sx);
        data.setYScale(data.yScale() * sy);
    }
}

void WindowAnimator::onAnimationStarted(KWin::EffectWindow* window, const PhosphorAnimation::WindowMotion& motion)
{
    if (window) {
        window->addRepaintFull();
    }
    qCDebug(lcEffect) << "Started animation from" << motion.startPosition << motion.startSize << "to"
                      << motion.targetGeometry.topLeft() << motion.targetGeometry.size()
                      << "duration:" << motion.duration;
}

void WindowAnimator::onAnimationComplete(KWin::EffectWindow* window, const PhosphorAnimation::WindowMotion&)
{
    // Window is already at its final position (moveResize was called
    // before the animation started); a final full repaint clears the
    // residual transform from the last frame.
    if (window && !window->isDeleted()) {
        window->addRepaintFull();
    }
    qCDebug(lcEffect) << "Window snap animation complete";
}

void WindowAnimator::onRepaintNeeded(KWin::EffectWindow*, const QRectF& bounds) const
{
    if (bounds.isValid()) {
        KWin::effects->addRepaint(bounds.toAlignedRect());
    }
}

bool WindowAnimator::isHandleValid(KWin::EffectWindow* window) const
{
    return window && !window->isDeleted();
}

QMarginsF WindowAnimator::expandedPadding(KWin::EffectWindow* window,
                                          const PhosphorAnimation::WindowMotion& motion) const
{
    // expandedGeometry includes shadow / decoration padding. Once a
    // window enters the "deleted" state its Item tree may be torn down
    // and expandedGeometry() would deref a null Item — fall back to the
    // bare target rect in that case.
    const QRectF expanded =
        (window && !window->isDeleted()) ? window->expandedGeometry() : QRectF(motion.targetGeometry);

    // Derive positive-margin padding from the expanded/frame delta.
    // Clamp each component to >= 0: if expanded is somehow smaller than
    // the frame (shouldn't happen, but decoration data can race), a
    // negative margin would silently shrink the repaint rect via
    // adjust() and cause visible paint truncation.
    const QRectF frameGeo(motion.targetGeometry);
    return QMarginsF(qMax(0.0, frameGeo.x() - expanded.x()), qMax(0.0, frameGeo.y() - expanded.y()),
                     qMax(0.0, expanded.right() - frameGeo.right()), qMax(0.0, expanded.bottom() - frameGeo.bottom()));
}

} // namespace PlasmaZones
