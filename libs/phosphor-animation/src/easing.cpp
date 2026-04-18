// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Easing.h>

#include <QLocale>
#include <QLoggingCategory>
#include <QStringList>
#include <QtMath>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcEasing, "phosphoranimation.easing")

// ═══════════════════════════════════════════════════════════════════════════════
// Elastic / bounce reference formulas
// ═══════════════════════════════════════════════════════════════════════════════

qreal Easing::evaluateElasticOut(qreal t, qreal amp, qreal per)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    const qreal a = qMax(1.0, amp);
    const qreal s = per / (2.0 * M_PI) * qAsin(1.0 / a);
    return a * qPow(2.0, -10.0 * t) * qSin((t - s) * 2.0 * M_PI / per) + 1.0;
}

qreal Easing::evaluateBounceOut(qreal t, qreal amp, int n)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    constexpr qreal r = 0.5;
    n = qBound(1, n, 8);

    const qreal S = (1.0 - qPow(r, n)) / (1.0 - r);
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
// Curve interface
// ═══════════════════════════════════════════════════════════════════════════════

QString Easing::typeId() const
{
    switch (type) {
    case Type::CubicBezier:
        return QStringLiteral("bezier");
    case Type::ElasticIn:
        return QStringLiteral("elastic-in");
    case Type::ElasticOut:
        return QStringLiteral("elastic-out");
    case Type::ElasticInOut:
        return QStringLiteral("elastic-in-out");
    case Type::BounceIn:
        return QStringLiteral("bounce-in");
    case Type::BounceOut:
        return QStringLiteral("bounce-out");
    case Type::BounceInOut:
        return QStringLiteral("bounce-in-out");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("bezier"));
}

qreal Easing::evaluate(qreal x) const
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
    case Type::ElasticInOut:
        if (x < 0.5)
            return (1.0 - evaluateElasticOut(1.0 - 2.0 * x, amplitude, period)) * 0.5;
        return evaluateElasticOut(2.0 * x - 1.0, amplitude, period) * 0.5 + 0.5;
    case Type::BounceOut:
        return evaluateBounceOut(x, amplitude, bounces);
    case Type::BounceIn:
        return 1.0 - evaluateBounceOut(1.0 - x, amplitude, bounces);
    case Type::BounceInOut:
        if (x < 0.5)
            return (1.0 - evaluateBounceOut(1.0 - 2.0 * x, amplitude, bounces)) * 0.5;
        return evaluateBounceOut(2.0 * x - 1.0, amplitude, bounces) * 0.5 + 0.5;
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

QString Easing::toString() const
{
    switch (type) {
    case Type::CubicBezier:
        return QStringLiteral("%1,%2,%3,%4")
            .arg(x1, 0, 'f', 2)
            .arg(y1, 0, 'f', 2)
            .arg(x2, 0, 'f', 2)
            .arg(y2, 0, 'f', 2);
    case Type::ElasticIn:
    case Type::ElasticOut:
    case Type::ElasticInOut:
        return QStringLiteral("%1:%2,%3").arg(typeId()).arg(amplitude, 0, 'f', 2).arg(period, 0, 'f', 2);
    case Type::BounceIn:
    case Type::BounceOut:
    case Type::BounceInOut:
        return QStringLiteral("%1:%2,%3").arg(typeId()).arg(amplitude, 0, 'f', 2).arg(bounces);
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("0.33,1.00,0.68,1.00"));
}

std::unique_ptr<Curve> Easing::clone() const
{
    return std::make_unique<Easing>(*this);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parsing + equality
// ═══════════════════════════════════════════════════════════════════════════════

Easing Easing::fromString(const QString& str)
{
    Easing curve;
    if (str.isEmpty())
        return curve;

    // Detect named curves (e.g. "elastic-in") vs numeric cubic-bezier spec
    // ("0.33,1.0,0.68,1.0" or scientific "1.5e-3,…"). The 'e'/'E' exemption
    // is for floating-point exponents — numeric strings must not be
    // misclassified as named. Named curves like "elastic-*" hit 'l' first
    // and still register as letter-bearing.
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

        if (name == QLatin1String("bezier")) {
            curve.type = Type::CubicBezier;
            // Params handled in the numeric path below by falling through.
        } else if (name == QLatin1String("elastic-in")) {
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
            qCDebug(lcEasing) << "unknown named curve" << name << "- using default";
            return curve;
        }

        if (!params.isEmpty()) {
            const QLocale cLocale = QLocale::c();
            const QStringList parts = params.split(QLatin1Char(','));

            if (curve.type == Type::CubicBezier) {
                if (parts.size() == 4) {
                    bool ok1, ok2, ok3, ok4;
                    qreal x1v = cLocale.toDouble(parts[0].trimmed(), &ok1);
                    qreal y1v = cLocale.toDouble(parts[1].trimmed(), &ok2);
                    qreal x2v = cLocale.toDouble(parts[2].trimmed(), &ok3);
                    qreal y2v = cLocale.toDouble(parts[3].trimmed(), &ok4);
                    if (ok1 && ok2 && ok3 && ok4) {
                        curve.x1 = qBound(0.0, x1v, 1.0);
                        curve.y1 = qBound(-1.0, y1v, 2.0);
                        curve.x2 = qBound(0.0, x2v, 1.0);
                        curve.y2 = qBound(-1.0, y2v, 2.0);
                    } else {
                        qCWarning(lcEasing) << "bezier params parse failure in" << str << "- using default curve";
                    }
                } else {
                    qCWarning(lcEasing) << "bezier expects 4 comma-separated params, got" << parts.size() << "in" << str
                                        << "- using default curve";
                }
            } else {
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
                    } else {
                        bool ok;
                        int b = cLocale.toInt(parts[1].trimmed(), &ok);
                        if (ok)
                            curve.bounces = qBound(1, b, 8);
                    }
                }
            }
        }
        return curve;
    }

    // Legacy bare-bezier form: "x1,y1,x2,y2"
    const QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() == 4) {
        const QLocale cLocale = QLocale::c();
        bool ok1, ok2, ok3, ok4;
        qreal x1v = cLocale.toDouble(parts[0].trimmed(), &ok1);
        qreal y1v = cLocale.toDouble(parts[1].trimmed(), &ok2);
        qreal x2v = cLocale.toDouble(parts[2].trimmed(), &ok3);
        qreal y2v = cLocale.toDouble(parts[3].trimmed(), &ok4);
        if (ok1 && ok2 && ok3 && ok4) {
            curve.x1 = qBound(0.0, x1v, 1.0);
            curve.y1 = qBound(-1.0, y1v, 2.0);
            curve.x2 = qBound(0.0, x2v, 1.0);
            curve.y2 = qBound(-1.0, y2v, 2.0);
        } else {
            qCDebug(lcEasing) << "invalid numeric values in" << str << "- using default";
        }
    } else {
        qCDebug(lcEasing) << "invalid format (expected x1,y1,x2,y2):" << str << "- using default";
    }
    return curve;
}

bool Easing::operator==(const Easing& other) const
{
    if (type != other.type)
        return false;
    if (type == Type::CubicBezier) {
        return qFuzzyCompare(1.0 + x1, 1.0 + other.x1) && qFuzzyCompare(1.0 + y1, 1.0 + other.y1)
            && qFuzzyCompare(1.0 + x2, 1.0 + other.x2) && qFuzzyCompare(1.0 + y2, 1.0 + other.y2);
    }
    if (type == Type::ElasticIn || type == Type::ElasticOut || type == Type::ElasticInOut) {
        return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude) && qFuzzyCompare(1.0 + period, 1.0 + other.period);
    }
    // Bounce*: amplitude + bounces
    return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude) && bounces == other.bounces;
}

bool Easing::equals(const Curve& other) const
{
    // Delegate to operator== so polymorphic equality matches the
    // value-type comparison exactly (no drift from lossy toString()).
    if (typeId() != other.typeId()) {
        return false;
    }
    const Easing* rhs = dynamic_cast<const Easing*>(&other);
    if (!rhs) {
        return false;
    }
    return *this == *rhs;
}

} // namespace PhosphorAnimation
