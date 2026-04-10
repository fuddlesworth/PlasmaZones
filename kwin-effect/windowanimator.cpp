// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <animation_math.h>

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>
#include <QLineF>
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
    if (!window || !m_enabled) {
        return false;
    }

    // Skip degenerate targets
    if (targetGeometry.width() <= 0 || targetGeometry.height() <= 0) {
        return false;
    }

    // Skip if position change is below the minimum distance threshold
    // and size isn't changing either
    const QPointF newPos = targetGeometry.topLeft();
    const qreal dist = QLineF(oldPosition, newPos).length();
    const bool sizeChanging =
        qAbs(oldSize.width() - targetGeometry.width()) > 1.0 || qAbs(oldSize.height() - targetGeometry.height()) > 1.0;
    if (dist < qMax(1.0, qreal(m_minDistance)) && !sizeChanging) {
        return false;
    }

    WindowAnimation anim;
    anim.startPosition = oldPosition;
    anim.startSize = oldSize;
    anim.targetGeometry = targetGeometry;
    anim.duration = m_duration;
    anim.easing = m_easing;
    // startTime is -1 (pending); latched to presentTime on first advanceAnimations()
    m_animations[window] = anim;

    window->addRepaintFull();

    qCDebug(lcEffect) << "Started animation from" << oldPosition << oldSize << "to" << newPos << targetGeometry.size()
                      << "distance:" << dist << "scale:" << sizeChanging << "duration:" << m_duration
                      << "easing:" << m_easing.toString();
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
    QRectF expanded = (window && !window->isDeleted()) ? window->expandedGeometry() : QRectF(it->targetGeometry);

    // Shadow/decoration padding (constant)
    const QRectF frameGeo(it->targetGeometry);
    const qreal padLeft = expanded.x() - frameGeo.x();
    const qreal padTop = expanded.y() - frameGeo.y();
    const qreal padRight = expanded.right() - frameGeo.right();
    const qreal padBottom = expanded.bottom() - frameGeo.bottom();

    // Footprint at animation start (t=0)
    QRectF atStart(it->startPosition.x() + padLeft, it->startPosition.y() + padTop,
                   it->startSize.width() - padLeft + padRight, it->startSize.height() - padTop + padBottom);

    QRectF bounds = expanded.united(atStart);

    // For overshooting easing curves, sample to find extremes.
    const bool isBounce =
        (it->easing.type == EasingCurve::Type::BounceIn || it->easing.type == EasingCurve::Type::BounceOut
         || it->easing.type == EasingCurve::Type::BounceInOut);
    const bool needsSampling =
        (it->easing.type == EasingCurve::Type::ElasticIn || it->easing.type == EasingCurve::Type::ElasticOut
         || it->easing.type == EasingCurve::Type::ElasticInOut)
        || (isBounce && it->easing.amplitude > 1.0)
        || (it->easing.type == EasingCurve::Type::CubicBezier
            && (it->easing.y1 < 0.0 || it->easing.y1 > 1.0 || it->easing.y2 < 0.0 || it->easing.y2 > 1.0));

    if (needsSampling) {
        const qreal dx = it->startPosition.x() - it->targetGeometry.x();
        const qreal dy = it->startPosition.y() - it->targetGeometry.y();
        const qreal dw = it->startSize.width() - it->targetGeometry.width();
        const qreal dh = it->startSize.height() - it->targetGeometry.height();

        constexpr int nSamples = 50;
        for (int i = 1; i < nSamples; ++i) {
            qreal p = it->easing.evaluate(qreal(i) / nSamples);
            const qreal inv = 1.0 - p;
            QRectF sampledRect(it->targetGeometry.x() + dx * inv + padLeft, it->targetGeometry.y() + dy * inv + padTop,
                               it->targetGeometry.width() + dw * inv - padLeft + padRight,
                               it->targetGeometry.height() + dh * inv - padTop + padBottom);
            bounds = bounds.united(sampledRect);
        }
    }

    bounds.adjust(-2.0, -2.0, 2.0, 2.0);
    return bounds;
}

} // namespace PlasmaZones
