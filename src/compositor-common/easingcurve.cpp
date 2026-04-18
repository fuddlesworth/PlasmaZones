// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "easingcurve.h"

#include "logging.h"

#include <QLocale>
#include <QStringList>
#include <QLoggingCategory>

namespace PlasmaZones {

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

    constexpr qreal r = 0.5;
    n = qBound(1, n, 8);

    qreal S = (1.0 - qPow(r, n)) / (1.0 - r);
    const qreal d = 1.0 / (1.0 + S);

    if (t < d) {
        const qreal u = t / d;
        return u * u;
    }

    qreal tAccum = d;
    for (int k = 0; k < n; ++k) {
        const qreal dk = d * qPow(r, k);
        if (t < tAccum + dk || k == n - 1) {
            const qreal u = (t - tAccum) / dk;
            const qreal height = qPow(r, 2 * (k + 1));
            const qreal dip = 1.0 - 4.0 * (u - 0.5) * (u - 0.5);
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

    // Detect named curves (e.g. "elastic-in") vs numeric cubic-bezier spec
    // (e.g. "0.33,1.0,0.68,1.0" or "1.5e-3,…"). The 'e'/'E' exemption is
    // specifically for floating-point scientific notation exponents so that
    // numeric strings are not misclassified as named curves — it is NOT
    // matching the leading 'e' of "elastic-*" (those hit 'l' first and set
    // hasLetter=true anyway).
    bool hasLetter = false;
    for (const QChar& ch : str) {
        if (ch.isLetter() && ch != QLatin1Char('e') && ch != QLatin1Char('E')) {
            hasLetter = true;
            break;
        }
    }

    if (hasLetter) {
        const int colonIdx = str.indexOf(QLatin1Char(':'));
        const QString name = (colonIdx >= 0) ? str.left(colonIdx).trimmed() : str.trimmed();
        const QString params = (colonIdx >= 0) ? str.mid(colonIdx + 1).trimmed() : QString();

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
            qCDebug(lcCompositorCommon) << "animationEasingCurve: unknown named curve" << name << "- using default";
            return curve;
        }

        if (!params.isEmpty()) {
            const QLocale cLocale = QLocale::c();
            const QStringList parts = params.split(QLatin1Char(','));
            if (parts.size() >= 1) {
                bool ok;
                qreal a = cLocale.toDouble(parts[0].trimmed(), &ok);
                if (ok)
                    curve.amplitude = qBound(0.5, a, 3.0);
            }
            if (parts.size() >= 2) {
                if (curve.type == Type::ElasticIn || curve.type == Type::ElasticOut
                    || curve.type == Type::ElasticInOut) {
                    bool ok;
                    qreal p = cLocale.toDouble(parts[1].trimmed(), &ok);
                    if (ok)
                        curve.period = qBound(0.1, p, 1.0);
                } else if (curve.type == Type::BounceIn || curve.type == Type::BounceOut
                           || curve.type == Type::BounceInOut) {
                    bool ok;
                    int b = cLocale.toInt(parts[1].trimmed(), &ok);
                    if (ok)
                        curve.bounces = qBound(1, b, 8);
                }
            }
        }
        return curve;
    }

    const QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() == 4) {
        const QLocale cLocale = QLocale::c();
        bool ok1, ok2, ok3, ok4;
        qreal x1 = cLocale.toDouble(parts[0].trimmed(), &ok1);
        qreal y1 = cLocale.toDouble(parts[1].trimmed(), &ok2);
        qreal x2 = cLocale.toDouble(parts[2].trimmed(), &ok3);
        qreal y2 = cLocale.toDouble(parts[3].trimmed(), &ok4);
        if (ok1 && ok2 && ok3 && ok4) {
            curve.x1 = qBound(0.0, x1, 1.0);
            curve.y1 = qBound(-1.0, y1, 2.0);
            curve.x2 = qBound(0.0, x2, 1.0);
            curve.y2 = qBound(-1.0, y2, 2.0);
        } else {
            qCDebug(lcCompositorCommon) << "animationEasingCurve: invalid numeric values in" << str
                                        << "- using default";
        }
    } else {
        qCDebug(lcCompositorCommon) << "animationEasingCurve: invalid format (expected x1,y1,x2,y2):" << str
                                    << "- using default";
    }
    return curve;
}

QString EasingCurve::toString() const
{
    switch (type) {
    case Type::CubicBezier:
        return QStringLiteral("%1,%2,%3,%4")
            .arg(x1, 0, 'f', 2)
            .arg(y1, 0, 'f', 2)
            .arg(x2, 0, 'f', 2)
            .arg(y2, 0, 'f', 2);
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
    qreal t = x;
    for (int i = 0; i < 8; ++i) {
        const qreal t2 = t * t;
        const qreal t3 = t2 * t;
        const qreal mt = 1.0 - t;
        const qreal mt2 = mt * mt;

        const qreal bx = 3.0 * mt2 * t * x1 + 3.0 * mt * t2 * x2 + t3 - x;
        const qreal dbx = 3.0 * mt2 * x1 + 6.0 * mt * t * (x2 - x1) + 3.0 * t2 * (1.0 - x2);

        if (qAbs(dbx) < 1e-12)
            break;
        t -= bx / dbx;
        t = qBound(0.0, t, 1.0);
    }

    const qreal mt = 1.0 - t;
    return 3.0 * mt * mt * t * y1 + 3.0 * mt * t * t * y2 + t * t * t;
}

} // namespace PlasmaZones
