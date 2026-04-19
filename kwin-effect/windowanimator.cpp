// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>

#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void WindowAnimator::setOutputClockResolver(OutputClockResolver resolver)
{
    m_outputClockResolver = std::move(resolver);
}

PhosphorAnimation::IMotionClock* WindowAnimator::clockForHandle(KWin::EffectWindow* window) const
{
    // Resolve via the effect-provided per-output map. Fall back to the
    // controller's default clock when:
    //   - no resolver is installed (tests, minimal embeddings),
    //   - the window has no current output (mid-migration, destroyed),
    //   - the resolver returns nullptr for an unknown output.
    if (m_outputClockResolver && window && !window->isDeleted()) {
        if (auto* clock = m_outputClockResolver(window->screen())) {
            return clock;
        }
    }
    return PhosphorAnimation::AnimationController<KWin::EffectWindow*>::clockForHandle(window);
}

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    // Symmetric with expandedPadding(): once a window is "deleted"
    // its Item tree is torn down and frameGeometry()/windowClass()
    // dereference a stale Item. slotWindowClosed / windowDeleted both
    // call removeAnimation(w) so this path is normally unreachable,
    // but mirroring the guard removes the latent hazard should
    // ordering change.
    if (!window || window->isDeleted()) {
        return;
    }

    const PhosphorAnimation::AnimatedValue<QRectF>* anim = animationFor(window);
    if (!anim || !anim->isAnimating()) {
        return;
    }

    const QRectF current = anim->value();

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
    //
    // Client-buffer caveat: `moveResize()` was applied synchronously at
    // animation start, so `frameGeometry().size()` is already at the
    // target. The Wayland client, however, only repaints its buffer to
    // the new size after acking the configure — typically one frame
    // later. During that window the buffer pixels are still painted at
    // `oldFrame.size()` but get stretched to `desiredSize` (which is
    // between old and target) via this scale. The artefact is at most
    // one frame of DPI-scaled content and settles the moment the
    // client's next commit lands. Fixing it properly would require
    // deferring moveResize until animation completion, which breaks
    // downstream expansionGeometry lookups during the animation — the
    // current tradeoff intentionally favours correct bounds over
    // pixel-perfect scaling of the transition's first frame.
    if (anim->hasSizeChange()) {
        const QSizeF desiredSize = current.size();
        const QSizeF actualSize = window->frameGeometry().size();
        // Minimum-dimension floor: prevents division-by-zero when
        // frameGeometry().size() is degenerate (newly-created window,
        // mid-move transient). 1.0 px is the smallest float we can
        // divide by without numerical blow-up at typical display sizes.
        constexpr qreal kMinActualDim = 1.0;
        // Visual-scale bounds: the factor `desiredSize / actualSize`
        // applied to the GL texture sampler. Upper bound 100× covers
        // a 4K (3840 px) window snapping to a 100 px thumbnail
        // (≈40×) with headroom; lower bound 0.01 covers the inverse
        // (thumbnail snapping to full-screen starts at 0.025 ≈ 1/40).
        // Both bounds are display-artefact guards, not physical
        // limits — a pathological settings UI writing e.g. a zero-
        // size target shouldn't produce NaN textures.
        constexpr qreal kMinScaleFactor = 0.01;
        constexpr qreal kMaxScaleFactor = 100.0;
        const qreal sx =
            qBound(kMinScaleFactor, desiredSize.width() / qMax(actualSize.width(), kMinActualDim), kMaxScaleFactor);
        const qreal sy =
            qBound(kMinScaleFactor, desiredSize.height() / qMax(actualSize.height(), kMinActualDim), kMaxScaleFactor);
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
