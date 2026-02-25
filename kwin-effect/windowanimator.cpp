// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effectwindow.h>
#include <QLoggingCategory>
#include <QStringList>

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

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QRectF& startGeometry, const QRect& endGeometry)
{
    if (!window || !m_enabled) {
        return false;
    }

    // If geometry is the same, no animation needed
    if (startGeometry.toRect() == endGeometry) {
        return false;
    }

    // Skip animation if geometry change is below the minimum distance threshold
    if (m_minDistance > 0) {
        qreal maxDelta =
            qMax(qMax(qAbs(endGeometry.x() - startGeometry.x()), qAbs(endGeometry.y() - startGeometry.y())),
                 qMax(qAbs(endGeometry.width() - startGeometry.width()),
                      qAbs(endGeometry.height() - startGeometry.height())));
        if (maxDelta < m_minDistance) {
            return false;
        }
    }

    WindowAnimation anim;
    anim.startGeometry = startGeometry;
    anim.endGeometry = QRectF(endGeometry);
    anim.duration = m_duration;
    anim.easing = m_easing;
    anim.timer.start();
    m_animations[window] = anim;

    // Request repaint to start animation
    window->addRepaintFull();

    qCDebug(lcEffect) << "Started animation from" << startGeometry << "to" << endGeometry << "duration:" << m_duration
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

bool WindowAnimator::isAnimationComplete(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return true; // No animation = complete
    }
    return it->isComplete();
}

QRectF WindowAnimator::currentGeometry(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry() : QRectF();
    }
    return it->currentGeometry();
}

QRect WindowAnimator::finalGeometry(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRect();
    }
    return it->endGeometry.toRect();
}

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data)
{
    auto it = m_animations.find(window);
    if (it == m_animations.end()) {
        return;
    }

    const WindowAnimation& anim = *it;

    // Validate animation state
    if (!anim.isValid()) {
        m_animations.erase(it);
        return;
    }

    QRectF current = anim.currentGeometry();
    QRectF original = window->frameGeometry();

    // Calculate translation offset
    data += QPointF(current.x() - original.x(), current.y() - original.y());

    // Calculate scale factors; use minimum divisor to avoid division by zero for very small windows
    constexpr qreal MinDimension = 1.0;
    const qreal w = qMax(original.width(), MinDimension);
    const qreal h = qMax(original.height(), MinDimension);
    data.setXScale(current.width() / w);
    data.setYScale(current.height() / h);
}

QRectF WindowAnimator::animationBounds(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRectF();
    }
    QRectF bounds = it->startGeometry.united(it->endGeometry);

    // Check if control points overshoot the [0,1] range
    if (it->easing.y1 < 0.0 || it->easing.y1 > 1.0 || it->easing.y2 < 0.0 || it->easing.y2 > 1.0) {
        // Compute actual overshoot from control points
        qreal maxOvershoot =
            qMax(qMax(qAbs(it->easing.y1 - 0.5) - 0.5, 0.0), qMax(qAbs(it->easing.y2 - 0.5) - 0.5, 0.0));
        qreal overshoot = qMax(0.4, maxOvershoot * 2.0); // at least 40%, scale with actual overshoot
        const qreal dx = qAbs(it->endGeometry.x() - it->startGeometry.x()) * overshoot;
        const qreal dy = qAbs(it->endGeometry.y() - it->startGeometry.y()) * overshoot;
        const qreal dw = qAbs(it->endGeometry.width() - it->startGeometry.width()) * overshoot;
        const qreal dh = qAbs(it->endGeometry.height() - it->startGeometry.height()) * overshoot;
        bounds.adjust(-dx - dw, -dy - dh, dx + dw, dy + dh);
    }
    return bounds;
}

bool WindowAnimator::isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return false;
    }
    return it->endGeometry.toRect() == targetGeometry;
}

void WindowAnimator::redirectAnimation(KWin::EffectWindow* window, const QRect& newTarget)
{
    auto it = m_animations.find(window);
    if (it == m_animations.end()) {
        return;
    }

    // Start new animation from current interpolated position
    WindowAnimation anim;
    anim.startGeometry = it->currentGeometry();
    anim.endGeometry = QRectF(newTarget);
    anim.duration = m_duration;
    anim.easing = m_easing;
    anim.timer.start();
    *it = anim;

    qCDebug(lcEffect) << "Redirected animation from" << anim.startGeometry << "to" << newTarget;
}

} // namespace PlasmaZones
