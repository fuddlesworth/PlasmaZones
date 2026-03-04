// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QLoggingCategory>
#include <QStringList>
#include <QLineF>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// Easing curve helpers — Elastic and Bounce formulas
// ═══════════════════════════════════════════════════════════════════════════════

qreal EasingCurve::evaluateElasticOut(qreal t, qreal amp, qreal per)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    const qreal a = qMax(1.0, amp);
    const qreal s = per / (2.0 * M_PI) * qAsin(1.0 / a);
    return a * qPow(2.0, -10.0 * t) * qSin((t - s) * 2.0 * M_PI / per) + 1.0;
}

qreal EasingCurve::evaluateBounceOut(qreal t, qreal amp, int n)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    // Generalized bounce with n bounce arcs after the initial rise.
    // Uses restitution coefficient r = 0.5 (each bounce reaches r² = 25% of
    // previous height, and takes r = 50% of the previous duration).
    //
    // Structure: one half-parabola (approach: 0→1) followed by n full parabolic
    // arcs of decreasing height. Amplitude scales the bounce dip depths.
    constexpr qreal r = 0.5;
    n = qBound(1, n, 8);

    // Compute base arc duration d so all arcs fit in [0, 1]:
    // total = d + d + d*r + d*r² + ... + d*r^(n-1) = d * (1 + (1-r^n)/(1-r))
    qreal S = (1.0 - qPow(r, n)) / (1.0 - r);
    const qreal d = 1.0 / (1.0 + S);

    // First half-arc: parabolic rise from 0 to 1
    if (t < d) {
        const qreal u = t / d;
        return u * u;
    }

    // Subsequent full arcs: each is a symmetric parabola peaking at y=1,
    // dipping to a floor that depends on the arc index.
    qreal tAccum = d;
    for (int k = 0; k < n; ++k) {
        const qreal dk = d * qPow(r, k);
        if (t < tAccum + dk || k == n - 1) {
            const qreal u = (t - tAccum) / dk;                    // 0..1 within this arc
            const qreal height = qPow(r, 2 * (k + 1));           // standard dip depth
            // Parabola: 1.0 at u=0 and u=1, dips to (1 - height*amp) at u=0.5
            const qreal dip = 1.0 - 4.0 * (u - 0.5) * (u - 0.5); // 0 at edges, 1 at center
            return 1.0 - height * amp * dip;
        }
        tAccum += dk;
    }
    return 1.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Easing curve parsing, serialization, evaluation
// ═══════════════════════════════════════════════════════════════════════════════

EasingCurve EasingCurve::fromString(const QString& str)
{
    EasingCurve curve;
    if (str.isEmpty())
        return curve;

    // Detect named curve: contains any letter other than scientific notation 'e'/'E'.
    // Without this guard, bezier strings like "1e-1,0.5,0.8,1.0" would be
    // misrouted to named-curve parsing and silently fall back to default.
    bool hasLetter = false;
    for (const QChar& ch : str) {
        if (ch.isLetter() && ch != QLatin1Char('e') && ch != QLatin1Char('E')) {
            hasLetter = true;
            break;
        }
    }

    if (hasLetter) {
        // Parse "name" or "name:p1,p2"
        const int colonIdx = str.indexOf(QLatin1Char(':'));
        const QString name = (colonIdx >= 0) ? str.left(colonIdx).trimmed() : str.trimmed();
        const QString params = (colonIdx >= 0) ? str.mid(colonIdx + 1).trimmed() : QString();

        // Map name to type
        if (name == QLatin1String("elastic-in")) {
            curve.type = Type::ElasticIn;
        } else if (name == QLatin1String("elastic-out")) {
            curve.type = Type::ElasticOut;
        } else if (name == QLatin1String("elastic-in-out")) {
            curve.type = Type::ElasticInOut;
        } else if (name == QLatin1String("bounce-in")) {
            curve.type = Type::BounceIn;
        } else if (name == QLatin1String("bounce-out")) {
            curve.type = Type::BounceOut;
        } else if (name == QLatin1String("bounce-in-out")) {
            curve.type = Type::BounceInOut;
        } else {
            qCDebug(lcEffect) << "animationEasingCurve: unknown named curve" << name << "- using default";
            return curve;
        }

        // Parse optional parameters
        if (!params.isEmpty()) {
            const QStringList parts = params.split(QLatin1Char(','));
            // First param is always amplitude (elastic and bounce)
            if (parts.size() >= 1) {
                bool ok;
                qreal a = parts[0].trimmed().toDouble(&ok);
                if (ok)
                    curve.amplitude = qBound(0.5, a, 3.0);
            }
            // Second param: period (elastic) or bounce count (bounce)
            if (parts.size() >= 2) {
                if (curve.type == Type::ElasticIn || curve.type == Type::ElasticOut || curve.type == Type::ElasticInOut) {
                    bool ok;
                    qreal p = parts[1].trimmed().toDouble(&ok);
                    if (ok)
                        curve.period = qBound(0.1, p, 1.0);
                } else if (curve.type == Type::BounceIn || curve.type == Type::BounceOut || curve.type == Type::BounceInOut) {
                    bool ok;
                    int b = parts[1].trimmed().toInt(&ok);
                    if (ok)
                        curve.bounces = qBound(1, b, 8);
                }
            }
        }
        return curve;
    }

    // Bezier: "x1,y1,x2,y2"
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
    } else {
        qCDebug(lcEffect) << "animationEasingCurve: invalid format (expected x1,y1,x2,y2):" << str << "- using default";
    }
    return curve;
}

QString EasingCurve::toString() const
{
    switch (type) {
    case Type::CubicBezier:
        return QStringLiteral("%1,%2,%3,%4").arg(x1, 0, 'f', 2).arg(y1, 0, 'f', 2).arg(x2, 0, 'f', 2).arg(y2, 0, 'f', 2);
    case Type::ElasticIn:
        return QStringLiteral("elastic-in:%1,%2").arg(amplitude, 0, 'f', 2).arg(period, 0, 'f', 2);
    case Type::ElasticOut:
        return QStringLiteral("elastic-out:%1,%2").arg(amplitude, 0, 'f', 2).arg(period, 0, 'f', 2);
    case Type::ElasticInOut:
        return QStringLiteral("elastic-in-out:%1,%2").arg(amplitude, 0, 'f', 2).arg(period, 0, 'f', 2);
    case Type::BounceIn:
        return QStringLiteral("bounce-in:%1,%2").arg(amplitude, 0, 'f', 2).arg(bounces);
    case Type::BounceOut:
        return QStringLiteral("bounce-out:%1,%2").arg(amplitude, 0, 'f', 2).arg(bounces);
    case Type::BounceInOut:
        return QStringLiteral("bounce-in-out:%1,%2").arg(amplitude, 0, 'f', 2).arg(bounces);
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("0.33,1.00,0.68,1.00"));
}

qreal EasingCurve::evaluate(qreal x) const
{
    if (x <= 0.0)
        return 0.0;
    if (x >= 1.0)
        return 1.0;

    switch (type) {
    case Type::ElasticOut:
        return evaluateElasticOut(x, amplitude, period);
    case Type::ElasticIn:
        return 1.0 - evaluateElasticOut(1.0 - x, amplitude, period);
    case Type::ElasticInOut: {
        if (x < 0.5) {
            return (1.0 - evaluateElasticOut(1.0 - 2.0 * x, amplitude, period)) * 0.5;
        }
        return evaluateElasticOut(2.0 * x - 1.0, amplitude, period) * 0.5 + 0.5;
    }
    case Type::BounceOut:
        return evaluateBounceOut(x, amplitude, bounces);
    case Type::BounceIn:
        return 1.0 - evaluateBounceOut(1.0 - x, amplitude, bounces);
    case Type::BounceInOut: {
        if (x < 0.5) {
            return (1.0 - evaluateBounceOut(1.0 - 2.0 * x, amplitude, bounces)) * 0.5;
        }
        return evaluateBounceOut(2.0 * x - 1.0, amplitude, bounces) * 0.5 + 0.5;
    }
    case Type::CubicBezier:
        break;
    }

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

        if (!it->isValid() || window->isDeleted()) {
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
    // Guard: once a window enters the "deleted" state, its Item tree may be
    // torn down and expandedGeometry() would dereference a null Item pointer
    // (SIGSEGV in KWin::Item::boundingRect).  Fall back to the stored target.
    QRectF expanded = (window && !window->isDeleted())
        ? window->expandedGeometry()
        : QRectF(it->targetGeometry);

    // The same visual footprint translated to the animation start position
    const QPointF maxOffset(it->startPosition.x() - it->targetGeometry.x(),
                            it->startPosition.y() - it->targetGeometry.y());
    QRectF atStart = expanded.translated(maxOffset);

    QRectF bounds = expanded.united(atStart);

    // For non-bezier curves or overshooting bezier curves, the translation
    // offset can overshoot past both start and end positions.
    // Sample the curve to find extremes and expand bounds accordingly.
    // Elastic curves overshoot; bounce curves with amplitude > 1.0 can dip below 0.
    // Bezier curves only overshoot when control points exceed [0,1] Y range.
    // Bounce with amplitude <= 1.0 is always in [0,1] — skip sampling.
    const bool isBounce = (it->easing.type == EasingCurve::Type::BounceIn
        || it->easing.type == EasingCurve::Type::BounceOut
        || it->easing.type == EasingCurve::Type::BounceInOut);
    const bool needsSampling =
        (it->easing.type == EasingCurve::Type::ElasticIn
         || it->easing.type == EasingCurve::Type::ElasticOut
         || it->easing.type == EasingCurve::Type::ElasticInOut)
        || (isBounce && it->easing.amplitude > 1.0)
        || (it->easing.type == EasingCurve::Type::CubicBezier
            && (it->easing.y1 < 0.0 || it->easing.y1 > 1.0
                || it->easing.y2 < 0.0 || it->easing.y2 > 1.0));

    if (needsSampling) {
        qreal pMin = 0.0, pMax = 1.0;
        constexpr int nSamples = 50;
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
