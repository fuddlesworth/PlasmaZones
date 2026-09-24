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

namespace {

/// Elastic's decay rate. The curve's envelope is `2^(-10t)`, so in the natural
/// exponential form `e^(-Lambda t)` the rate is 10*ln(2). Kept in this form
/// because the crest solution below is stated in terms of it.
const qreal ElasticLambda = 10.0 * M_LN2;

/// The two period-dependent terms the crest solution needs.
///
/// Elastic is `1 + g * e^(-Lambda t) * sin(w (t - s))`, an exponentially decaying
/// sine riding on the target. Its first crest is NOT at the sine's own crest — the
/// envelope is falling underneath it, which drags the maximum earlier. Solving
/// `d/dt [e^(-Lambda t) sin(w (t - s))] = 0` gives `tan(w (t - s)) = w / Lambda`,
/// so the crest sits at phase `theta = atan(w / Lambda)` past the pin, i.e. at
///
///     t_crest = s + theta / w
///
/// which is exact, not an approximation. `beta` is Lambda/w, the decay per radian.
struct ElasticCrest
{
    qreal beta; ///< Lambda / w
    qreal theta; ///< atan(w / Lambda) — the crest's phase
    qreal coefficient; ///< e^(-beta * theta) * sin(theta)
};

ElasticCrest elasticCrest(qreal period)
{
    const qreal beta = ElasticLambda * period / (2.0 * M_PI);
    const qreal theta = qAtan(1.0 / beta);
    return {beta, theta, qExp(-beta * theta) * qSin(theta)};
}

/// Peak value reached by elastic-out for internal gain @p gain at @p period.
///
/// Substituting t_crest back into the curve, and using the pin `s = asin(1/g) / w`
/// that forces f(0) = 0:
///
///     peak = 1 + g * e^(-beta * (asin(1/g) + theta)) * sin(theta)
///
/// Verified against a brute-force scan of the curve to within 1e-9.
qreal elasticPeakForGain(qreal gain, const ElasticCrest& c)
{
    return 1.0 + gain * qExp(-c.beta * qAsin(1.0 / gain)) * c.coefficient;
}

} // namespace

qreal Easing::minElasticPeak(qreal period)
{
    // The curve must START at 0, a full unit below the target, so the wave's
    // magnitude at t = 0 can never be less than 1 — that is what `asin(1/g)`
    // encodes, and it is why the gain floors at 1. The peak that gain produces is
    // therefore the gentlest bounce the curve can make at this period, and at a
    // short period the crest arrives before the envelope has decayed much, so that
    // floor is HIGH (about 1.71 at period 0.1, versus 1.05 at period 1.0).
    //
    // This is not a limitation the reparametrisation introduced; it is a property
    // of the curve that the old amplitude parameter merely hid. Callers surface it
    // as the low end of the overshoot range rather than silently clamping into it.
    return elasticPeakForGain(1.0, elasticCrest(qBound(MinElasticPeriod, period, MaxElasticPeriod)));
}

qreal Easing::solveElasticGain(qreal peak, qreal period)
{
    // One-entry memo. The bisection costs ~800 ns, and AnimatedValue's swept-bounds
    // sampler calls evaluate() 49 times in a tight loop on the SAME curve every frame
    // it is animating — so without this the gain is re-solved 50x per frame per window
    // for an answer that cannot have changed. Keyed on the exact inputs and holding
    // only a pure function of them, so a hit is indistinguishable from a miss; the
    // memo is thread_local because curves are shared across threads (compositor, QML,
    // daemon) and a shared cache would be a data race for no benefit.
    //
    // A single entry suffices precisely BECAUSE the callers are bursty: interleaving
    // two elastic curves costs one extra solve per burst, not per call.
    thread_local qreal memoPeak = qQNaN();
    thread_local qreal memoPeriod = qQNaN();
    thread_local qreal memoGain = 1.0;
    if (peak == memoPeak && period == memoPeriod) {
        return memoGain;
    }

    const ElasticCrest c = elasticCrest(qBound(MinElasticPeriod, period, MaxElasticPeriod));
    const qreal target = qBound(elasticPeakForGain(1.0, c), peak, MaxElasticPeak);

    // Bisection, not Newton: the peak is smooth and strictly increasing in the gain,
    // but its derivative carries a `1 / sqrt(g^2 - 1)` term that blows up as g -> 1,
    // which is exactly the floor case. Newton overshoots there and needs guarding;
    // bisection just works, and a fixed iteration count keeps it branch-predictable.
    //
    // The bracket is safe by construction: the largest gain any admitted (peak,
    // period) pair needs is ~4.4, at peak 2.0 and period 1.0.
    qreal lo = 1.0;
    qreal hi = 16.0;
    for (int i = 0; i < ElasticSolveIterations; ++i) {
        const qreal mid = 0.5 * (lo + hi);
        if (elasticPeakForGain(mid, c) < target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    const qreal gain = 0.5 * (lo + hi);
    memoPeak = peak;
    memoPeriod = period;
    memoGain = gain;
    return gain;
}

qreal Easing::clampAmplitude(Type t, qreal amp, qreal period)
{
    switch (t) {
    case Type::ElasticIn:
    case Type::ElasticOut:
    case Type::ElasticInOut:
        // Elastic's amplitude IS the peak the curve reaches, so its bounds are the
        // curve's own reachable range: the ceiling is the overshoot envelope (which
        // makes it exact rather than a compromise) and the floor is whatever the
        // gentlest bounce at this period happens to be.
        return qBound(minElasticPeak(period), amp, MaxElasticPeak);
    default:
        // Bounce never leaves [0, 1] at any amplitude, so the envelope has no claim
        // on it and its value scales dip depth over a range that is entirely live.
        return qBound(0.5, amp, 3.0);
    }
}

qreal Easing::evaluateElasticOut(qreal t, qreal peak, qreal per)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    // Clamp here and not only at the parse/setter clamps: `Easing` is a POD with
    // public fields, so a direct `curve.amplitude = 9.0` reaches evaluate() without
    // passing either. The clamp is also what guarantees the curve cannot leave the
    // overshoot envelope, since the clamped peak IS the curve's maximum.
    const qreal period = qBound(MinElasticPeriod, per, MaxElasticPeriod);
    const qreal g = solveElasticGain(peak, period);
    const qreal s = period / (2.0 * M_PI) * qAsin(1.0 / g);
    return g * qExp(-ElasticLambda * t) * qSin((t - s) * 2.0 * M_PI / period) + 1.0;
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

bool Easing::overshoots() const
{
    switch (type) {
    case Type::ElasticIn:
    case Type::ElasticOut:
    case Type::ElasticInOut:
        return true; // elastic always rings beyond [0, 1]
    case Type::BounceIn:
    case Type::BounceOut:
    case Type::BounceInOut:
        // Bounce never leaves [0,1], at ANY admitted amplitude. bounce-out is
        // `1 - height*amp*dip` with height <= 0.25 (r = 0.5), amp clamped to <= 3.0
        // and dip in [0,1], so the output floors at 0.25; bounce-in mirrors it. The
        // old `amplitude > 1.0` was a false positive, and it is not free: it made
        // every bounce-* animation pay a 49-sample evaluate() sweep in
        // AnimatedValue's swept-bounds, once per animating window per frame.
        return false;
    case Type::CubicBezier:
        // Bezier overshoots iff a y control point leaves [0, 1].
        return y1 < 0.0 || y1 > 1.0 || y2 < 0.0 || y2 > 1.0;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parsing + equality
// ═══════════════════════════════════════════════════════════════════════════════

Easing Easing::fromString(const QString& str)
{
    Easing curve;
    if (str.isEmpty())
        return curve;

    // Detect named curves (e.g. "elastic-in") vs numeric cubic-bezier
    // wire format ("0.33,1.0,0.68,1.0" or scientific "1.5e-3,…"). The
    // 'e'/'E' exemption is for floating-point exponents — numeric
    // strings must not be misclassified as named. Named curves like
    // "elastic-*" hit 'l' first and still register as letter-bearing.
    bool hasLetter = false;
    for (const QChar& ch : str) {
        if (ch.isLetter() && ch != QLatin1Char('e') && ch != QLatin1Char('E')) {
            hasLetter = true;
            break;
        }
    }

    if (hasLetter) {
        // Named curves (elastic-*, bounce-*) take the form "name:params".
        // Cubic-bezier uses the bare 4-comma wire format and is handled
        // below — `"bezier:..."` is intentionally NOT accepted; there is
        // exactly one wire format per curve type.
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
            qCDebug(lcEasing) << "unknown named curve" << name << "- using default";
            return curve;
        }

        // Read the raw amplitude but do NOT clamp it yet. Elastic's amplitude is its
        // peak, and the reachable floor depends on the period — which is the NEXT
        // field. Clamping in field order would bound it against the default period
        // rather than the one being parsed.
        qreal rawAmplitude = curve.amplitude;
        if (!params.isEmpty()) {
            const QLocale cLocale = QLocale::c();
            const QStringList parts = params.split(QLatin1Char(','));

            if (parts.size() >= 1) {
                bool ok;
                const qreal a = cLocale.toDouble(parts[0].trimmed(), &ok);
                if (ok)
                    rawAmplitude = a;
            }
            if (parts.size() >= 2) {
                if (curve.type == Type::ElasticIn || curve.type == Type::ElasticOut
                    || curve.type == Type::ElasticInOut) {
                    bool ok;
                    qreal p = cLocale.toDouble(parts[1].trimmed(), &ok);
                    if (ok)
                        curve.period = qBound(MinElasticPeriod, p, MaxElasticPeriod);
                } else {
                    bool ok;
                    int b = cLocale.toInt(parts[1].trimmed(), &ok);
                    if (ok)
                        curve.bounces = qBound(1, b, 8);
                }
            }
        }
        // Clamp unconditionally, INCLUDING the no-params case (a bare "elastic-out").
        // The struct's default amplitude of 1.0 is below every elastic floor, so
        // leaving it unclamped there would store a value the curve cannot produce and
        // then silently render a different one — evaluate() clamps regardless. Stored
        // has to equal used, or the settings UI reads back a number that is not what
        // the compositor is drawing.
        curve.amplitude = clampAmplitude(curve.type, rawAmplitude, curve.period);
        return curve;
    }

    // Cubic-bezier wire format: "x1,y1,x2,y2"
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
