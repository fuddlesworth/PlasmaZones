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
    const PhosphorAnimation::AnimatedValue<QRectF>* anim = animationFor(window);
    if (!anim || !anim->isAnimating()) {
        return;
    }

    const QRectF current = anim->value();
    const QRectF target = anim->to();

    // Translate: desired visual top-left offset from the actual
    // frameGeometry. moveResize() was called once to set the final
    // geometry, so frameGeometry is at the target. The offset shrinks
    // to zero as the animation completes.
    const QPointF desiredPos = current.topLeft();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);

    // Scale: smoothly morph from old size to target size.
    // visual_size = frameGeometry.size * scale = desiredSize.
    // Scale converges to 1.0 at t=1, so the final state uses the
    // natural buffer with no transform applied.
    const QSizeF desiredSize = current.size();
    const QSizeF targetSize = target.size();
    const bool sizeChanging =
        qAbs(desiredSize.width() - targetSize.width()) > 0.5 || qAbs(desiredSize.height() - targetSize.height()) > 0.5;
    if (sizeChanging) {
        const QSizeF actualSize = window->frameGeometry().size();
        constexpr qreal MinDim = 1.0;
        // [0.01, 100] is wide enough that a 4K window snapping to a
        // 100 px thumbnail (40× scale) renders without distortion. The
        // qMax(actualSize, MinDim) already prevents division
        // explosion, so this clamp only catches genuinely degenerate
        // input — keep it generous.
        const qreal sx = qBound(0.01, desiredSize.width() / qMax(actualSize.width(), MinDim), 100.0);
        const qreal sy = qBound(0.01, desiredSize.height() / qMax(actualSize.height(), MinDim), 100.0);
        data.setXScale(data.xScale() * sx);
        data.setYScale(data.yScale() * sy);
    }
}

void WindowAnimator::onAnimationStarted(KWin::EffectWindow* window,
                                        const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    if (window) {
        window->addRepaintFull();
    }
    qCDebug(lcEffect) << "Started animation from" << anim.from() << "to" << anim.to()
                      << "duration:" << anim.spec().profile.effectiveDuration();
}

void WindowAnimator::onAnimationComplete(KWin::EffectWindow*, const PhosphorAnimation::AnimatedValue<QRectF>&)
{
    // The controller fires onRepaintNeeded(bounds) immediately after
    // this hook returns, and those bounds already cover the full
    // animation path (start ∪ target + any overshoot, widened by
    // expandedPadding). A redundant addRepaintFull() here would be
    // strictly subsumed by the targeted bounds repaint, so this is
    // just a log anchor. Kept as a hook override so future adapters
    // can splice in completion-time work without touching the
    // controller.
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
                                          const PhosphorAnimation::AnimatedValue<QRectF>& anim) const
{
    // expandedGeometry includes shadow / decoration padding. Once a
    // window enters the "deleted" state its Item tree may be torn down
    // and expandedGeometry() would deref a null Item — fall back to the
    // bare target rect in that case.
    const QRectF expanded = (window && !window->isDeleted()) ? QRectF(window->expandedGeometry()) : anim.to();

    // Derive positive-margin padding from the expanded/frame delta.
    // Clamp each component to >= 0: if expanded is somehow smaller than
    // the frame (shouldn't happen, but decoration data can race), a
    // negative margin would silently shrink the repaint rect via
    // marginsAdded() and cause visible paint truncation.
    const QRectF frameGeo = anim.to();
    return QMarginsF(qMax(0.0, frameGeo.x() - expanded.x()), qMax(0.0, frameGeo.y() - expanded.y()),
                     qMax(0.0, expanded.right() - frameGeo.right()), qMax(0.0, expanded.bottom() - frameGeo.bottom()));
}

} // namespace PlasmaZones
