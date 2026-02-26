// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>
#include <QStringList>
#include <QLineF>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Cubic bezier evaluation
// ═══════════════════════════════════════════════════════════════════════════════

CubicBezierCurve CubicBezierCurve::fromString(const QString& str)
{
    CubicBezierCurve curve;
    const QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() == 4) {
        bool ok1, ok2, ok3, ok4;
        qreal x1 = parts[0].trimmed().toDouble(&ok1);
        qreal y1 = parts[1].trimmed().toDouble(&ok2);
        qreal x2 = parts[2].trimmed().toDouble(&ok3);
        qreal y2 = parts[3].trimmed().toDouble(&ok4);
        if (ok1 && ok2 && ok3 && ok4) {
            curve.x1 = qBound(0.0, x1, 1.0);
            curve.y1 = qBound(-1.0, y1, 2.0);
            curve.x2 = qBound(0.0, x2, 1.0);
            curve.y2 = qBound(-1.0, y2, 2.0);
        } else {
            qCDebug(lcEffect) << "animationEasingCurve: invalid numeric values in" << str << "- using default";
        }
    } else if (!str.isEmpty()) {
        qCDebug(lcEffect) << "animationEasingCurve: invalid format (expected x1,y1,x2,y2):" << str << "- using default";
    }
    return curve;
}

QString CubicBezierCurve::toString() const
{
    return QStringLiteral("%1,%2,%3,%4").arg(x1, 0, 'f', 2).arg(y1, 0, 'f', 2).arg(x2, 0, 'f', 2).arg(y2, 0, 'f', 2);
}

qreal CubicBezierCurve::evaluate(qreal x) const
{
    if (x <= 0.0)
        return 0.0;
    if (x >= 1.0)
        return 1.0;

    // Newton's method to solve Bx(t) = x for parameter t
    // Bx(t) = 3(1-t)^2*t*x1 + 3(1-t)*t^2*x2 + t^3
    qreal t = x; // initial guess
    for (int i = 0; i < 8; ++i) {
        const qreal t2 = t * t;
        const qreal t3 = t2 * t;
        const qreal mt = 1.0 - t;
        const qreal mt2 = mt * mt;

        // Bx(t) - x
        const qreal bx = 3.0 * mt2 * t * x1 + 3.0 * mt * t2 * x2 + t3 - x;
        // Bx'(t)
        const qreal dbx = 3.0 * mt2 * x1 + 6.0 * mt * t * (x2 - x1) + 3.0 * t2 * (1.0 - x2);

        if (qAbs(dbx) < 1e-12)
            break;
        t -= bx / dbx;
        t = qBound(0.0, t, 1.0);
    }

    // Evaluate By(t) with the solved parameter
    const qreal mt = 1.0 - t;
    return 3.0 * mt * mt * t * y1 + 3.0 * mt * t * t * y2 + t * t * t;
}

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

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QRect& targetGeometry)
{
    if (!window || !m_enabled) {
        return false;
    }

    // Skip if position change is below the minimum distance threshold
    const QPointF newPos = targetGeometry.topLeft();
    const qreal dist = QLineF(oldPosition, newPos).length();
    if (dist < qMax(1.0, qreal(m_minDistance))) {
        return false;
    }

    WindowAnimation anim;
    anim.startPosition = oldPosition;
    anim.targetGeometry = targetGeometry;
    anim.duration = m_duration;
    anim.easing = m_easing;
    anim.timer.start();
    m_animations[window] = anim;

    window->addRepaintFull();

    qCDebug(lcEffect) << "Started slide animation from" << oldPosition << "to" << newPos
                      << "distance:" << dist << "duration:" << m_duration
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

void WindowAnimator::advanceAnimations()
{
    // Iterate over a snapshot of keys — signal handlers triggered by repaint
    // could indirectly modify m_animations.
    const auto windows = m_animations.keys();
    for (KWin::EffectWindow* window : windows) {
        auto it = m_animations.find(window);
        if (it == m_animations.end()) {
            continue;
        }

        if (!it->isValid()) {
            m_animations.erase(it);
            continue;
        }

        if (it->isComplete()) {
            // Animation done — window is already at its final position
            // (moveResize was called before the animation started).
            // Clean up and repaint the full animation path.
            const QRectF bounds = animationBounds(window);
            m_animations.erase(it);
            window->addRepaintFull();
            if (bounds.isValid()) {
                KWin::effects->addRepaint(bounds.toAlignedRect());
            }
            qCDebug(lcEffect) << "Window slide animation complete";
        }
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

    // On Wayland, moveResize() triggers a configure/ack cycle so frameGeometry()
    // may lag behind the target for several frames. Using a relative offset from
    // frameGeometry() produces a "doubled offset" glitch until geometry catches up.
    // Computing absolute lerped position + offset-from-actual avoids this entirely.
    const QPointF desiredPos = it->currentVisualPosition();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);
}

QRectF WindowAnimator::animationBounds(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRectF();
    }

    // The window's expanded geometry (includes shadow/decoration padding)
    // at its real post-moveResize position.
    QRectF expanded = window ? window->expandedGeometry() : QRectF(it->targetGeometry);

    // The same visual footprint translated to the animation start position
    const QPointF maxOffset(it->startPosition.x() - it->targetGeometry.x(),
                            it->startPosition.y() - it->targetGeometry.y());
    QRectF atStart = expanded.translated(maxOffset);

    QRectF bounds = expanded.united(atStart);

    // For overshooting easing curves (control points outside [0,1]), the
    // translation offset can overshoot past both start and end positions.
    // Sample the curve to find extremes and expand bounds accordingly.
    if (it->easing.y1 < 0.0 || it->easing.y1 > 1.0 || it->easing.y2 < 0.0 || it->easing.y2 > 1.0) {
        qreal pMin = 0.0, pMax = 1.0;
        constexpr int nSamples = 10;
        for (int i = 1; i < nSamples; ++i) {
            qreal p = it->easing.evaluate(qreal(i) / nSamples);
            pMin = qMin(pMin, p);
            pMax = qMax(pMax, p);
        }
        // Offset at eased value p: maxOffset * (1 - p)
        if (pMax > 1.0) {
            bounds = bounds.united(expanded.translated(maxOffset * (1.0 - pMax)));
        }
        if (pMin < 0.0) {
            bounds = bounds.united(expanded.translated(maxOffset * (1.0 - pMin)));
        }
    }

    bounds.adjust(-2.0, -2.0, 2.0, 2.0);
    return bounds;
}

} // namespace PlasmaZones
