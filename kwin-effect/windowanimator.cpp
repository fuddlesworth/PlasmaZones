// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <PhosphorAnimation/AnimationMath.h>

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>
#include <QMarginsF>
#include <QVarLengthArray>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// WindowAnimator
// ═══════════════════════════════════════════════════════════════════════════════

WindowAnimator::WindowAnimator(QObject* parent)
    : QObject(parent)
{
}

bool WindowAnimator::hasAnimation(KWin::EffectWindow* window) const
{
    return m_animations.contains(window);
}

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                                    const QRect& targetGeometry)
{
    if (!window) {
        return false;
    }

    // All the skip-or-accept logic lives in AnimationMath::createSnapMotion
    // — DRY with every other PhosphorAnimation consumer and guarantees
    // that "worth animating?" decisions stay in one place.
    PhosphorAnimation::AnimationConfig config;
    config.enabled = m_enabled;
    config.duration = m_duration;
    config.easing = m_easing;
    config.minDistance = m_minDistance;
    auto motion = PhosphorAnimation::AnimationMath::createSnapMotion(oldPosition, oldSize, targetGeometry, config);
    if (!motion) {
        return false;
    }

    m_animations[window] = *motion;
    window->addRepaintFull();

    qCDebug(lcEffect) << "Started animation from" << oldPosition << oldSize << "to" << targetGeometry.topLeft()
                      << targetGeometry.size() << "duration:" << m_duration << "easing:" << m_easing.toString();
    return true;
}

void WindowAnimator::removeAnimation(KWin::EffectWindow* window)
{
    m_animations.remove(window);
}

void WindowAnimator::clear()
{
    m_animations.clear();
}

bool WindowAnimator::isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return false;
    }
    return it->targetGeometry == targetGeometry;
}

QPointF WindowAnimator::currentVisualPosition(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry().topLeft() : QPointF();
    }
    return it->currentVisualPosition();
}

QSizeF WindowAnimator::currentVisualSize(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry().size() : QSizeF();
    }
    return it->currentVisualSize();
}

void WindowAnimator::advanceAnimations(std::chrono::milliseconds presentTime)
{
    // Collect removals on the stack instead of heap-allocating QHash::keys() per frame
    QVarLengthArray<KWin::EffectWindow*, 16> toRemove;

    for (auto it = m_animations.begin(); it != m_animations.end(); ++it) {
        KWin::EffectWindow* window = it.key();

        if (window->isDeleted()) {
            toRemove.append(window);
            continue;
        }

        // Update cached progress from presentTime (latches startTime on first call)
        it->updateProgress(presentTime);

        if (it->isComplete(presentTime)) {
            // Animation done — window is already at its final position
            // (moveResize was called before the animation started).
            const QRectF bounds = animationBounds(window);
            toRemove.append(window);
            window->addRepaintFull();
            if (bounds.isValid()) {
                KWin::effects->addRepaint(bounds.toAlignedRect());
            }
            qCDebug(lcEffect) << "Window snap animation complete";
        }
    }

    for (KWin::EffectWindow* w : toRemove) {
        m_animations.remove(w);
    }
}

void WindowAnimator::scheduleRepaints() const
{
    for (auto it = m_animations.constBegin(); it != m_animations.constEnd(); ++it) {
        const QRectF bounds = animationBounds(it.key());
        if (bounds.isValid()) {
            KWin::effects->addRepaint(bounds.toAlignedRect());
        }
    }
}

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd() || !it->isValid()) {
        return;
    }

    // Translate: compute the desired visual position and offset from
    // the actual frameGeometry. moveResize() was called once to set the
    // final geometry, so frameGeometry should be at the target. The
    // offset shrinks to zero as the animation completes.
    const QPointF desiredPos = it->currentVisualPosition();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);

    // Scale: smoothly morph from old size to target size.
    // visual_size = frameGeometry.size * scale = desiredSize.
    // Scale converges to 1.0 at t=1, so the final state uses the
    // natural buffer with no transform applied.
    if (it->hasScaleChange()) {
        const QSizeF desiredSize = it->currentVisualSize();
        const QSizeF actualSize = window->frameGeometry().size();
        constexpr qreal MinDim = 1.0;
        const qreal sx = qBound(0.1, desiredSize.width() / qMax(actualSize.width(), MinDim), 10.0);
        const qreal sy = qBound(0.1, desiredSize.height() / qMax(actualSize.height(), MinDim), 10.0);
        data.setXScale(data.xScale() * sx);
        data.setYScale(data.yScale() * sy);
    }
}

QRectF WindowAnimator::animationBounds(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRectF();
    }

    // The window's expanded geometry (includes shadow/decoration padding).
    // Guard: once a window enters the "deleted" state, its Item tree may be
    // torn down and expandedGeometry() would dereference a null Item pointer.
    const QRectF expanded = (window && !window->isDeleted()) ? window->expandedGeometry() : QRectF(it->targetGeometry);

    // Derive positive-margin padding from the expanded/frame delta:
    //   expanded extends LEFT of frame  => expanded.x() < frame.x(), so margin.left = frame.x - expanded.x
    //   expanded extends RIGHT of frame => expanded.right() > frame.right(), so margin.right = expanded.right -
    //   frame.right
    //
    // Clamp each component to >= 0. If expanded is somehow smaller than the
    // frame (shouldn't happen, but decoration data can race), a negative
    // margin would silently shrink the repaint rect via adjust() and cause
    // visible paint truncation.
    const QRectF frameGeo(it->targetGeometry);
    const QMarginsF padding(qMax(0.0, frameGeo.x() - expanded.x()), qMax(0.0, frameGeo.y() - expanded.y()),
                            qMax(0.0, expanded.right() - frameGeo.right()),
                            qMax(0.0, expanded.bottom() - frameGeo.bottom()));

    return PhosphorAnimation::AnimationMath::repaintBounds(it->startPosition, it->startSize, it->targetGeometry,
                                                           it->easing, padding);
}

} // namespace PlasmaZones
