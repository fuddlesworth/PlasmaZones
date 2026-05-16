// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Spring.h>

#include <QLocale>
#include <QLoggingCategory>
#include <QStringList>
#include <QtMath>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcSpring, "phosphoranimation.spring")

// Step-response 2% settling time for a second-order system. Settling time
// is capped at MaxSettleSeconds to keep repaint budgets and timer delays
// bounded even for pathological parameters (omega near 0 or zeta huge).
static constexpr qreal MaxSettleSeconds = 30.0;

// 2% settling coefficient for a critically damped second-order system.
// Solves (1 + ωt)·exp(-ωt) = 0.02 for ωt numerically → ωt ≈ 5.834. The
// previous code used 5.0, which gave envelope(settleTime) ≈ 0.04 (twice
// the target band) — at zeta=1 this made `evaluate(1)` sit ~4% below the
// target instead of the advertised 2%.
static constexpr qreal CriticalSettleFactor = 5.834;

// Convergence threshold for step()-driven animation: when value is within
// ConvergeValueEps of target AND |velocity| < ConvergeVelEps, the spring
// is considered settled and further steps produce no change. These are
// in normalized units (step-response from 0 to 1).
static constexpr qreal ConvergeValueEps = 1.0e-4;
static constexpr qreal ConvergeVelEps = 1.0e-3;

// Near-unity damping tolerance — values of zeta within this window of
// 1.0 are treated as critically damped. A tighter qFuzzyCompare tolerance
// (~2e-12) would route zeta = 0.999 into the underdamped branch, where
// sqrt(1 - zeta²) ≈ 0.045 causes numerically ill-conditioned division.
static constexpr qreal CriticalDampingEpsilon = 1.0e-3;

// ═══════════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════════

Spring::Spring(qreal omegaVal, qreal zetaVal)
    : omega(qBound(0.1, omegaVal, 200.0))
    , zeta(qBound(0.0, zetaVal, 10.0))
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Curve interface
// ═══════════════════════════════════════════════════════════════════════════════

QString Spring::typeId() const
{
    return QStringLiteral("spring");
}

qreal Spring::evaluate(qreal t) const
{
    if (t <= 0.0)
        return 0.0;

    // Map normalized t to real seconds via the settle-time domain. The
    // spring's analytical position at real-time τ is evaluated here; at
    // t==1 we've reached settle time and the function is within the 2%
    // settle band (see settleTime()). The analytical step response is
    // evaluated all the way through t=1 — no special cased "if (t >=
    // 1.0) return 1.0;" — so there is no visible step-to-unity
    // discontinuity near the endpoint.
    const qreal tau = t * settleTime();

    if (zeta < 1.0 - CriticalDampingEpsilon) {
        // Underdamped: classic bouncy step response.
        const qreal oneMinusZetaSq = 1.0 - zeta * zeta;
        const qreal omegaD = omega * qSqrt(oneMinusZetaSq);
        const qreal envelope = qExp(-zeta * omega * tau);
        const qreal phase = omegaD * tau;
        const qreal ratio = zeta / qSqrt(oneMinusZetaSq);
        return 1.0 - envelope * (qCos(phase) + ratio * qSin(phase));
    }
    if (zeta <= 1.0 + CriticalDampingEpsilon) {
        // Critically (or near-critically) damped: no oscillation, fastest
        // settle. Widening the band avoids ill-conditioned division at
        // zeta very close to 1.
        const qreal envelope = qExp(-omega * tau);
        return 1.0 - envelope * (1.0 + omega * tau);
    }
    // Overdamped: two real exponential modes.
    const qreal disc = qSqrt(zeta * zeta - 1.0);
    const qreal p1 = omega * (zeta - disc); // slow mode
    const qreal p2 = omega * (zeta + disc); // fast mode
    const qreal c1 = (zeta + disc) / (2.0 * disc);
    const qreal c2 = (zeta - disc) / (2.0 * disc);
    return 1.0 - (c1 * qExp(-p1 * tau) - c2 * qExp(-p2 * tau));
}

void Spring::step(qreal dt, CurveState& state, qreal target) const
{
    if (dt <= 0.0)
        return;

    // Convergence check — when both position and velocity are below the
    // epsilons, lock to target and zero velocity. Prevents infinitesimal
    // vibration from accumulating after thousands of frames.
    const qreal error = target - state.value;
    if (qAbs(error) < ConvergeValueEps && qAbs(state.velocity) < ConvergeVelEps) {
        state.value = target;
        state.velocity = 0.0;
        state.time += dt;
        return;
    }

    // Semi-implicit (symplectic) Euler: acceleration → velocity → position.
    // Stable for dt < 1/(5·omega); at omega=30 that's ~6.6 ms, below the
    // 16 ms frame budget at 60 Hz. Callers that want hard guarantees for
    // extreme stiffness should substep externally.
    const qreal stiffness = omega * omega;
    const qreal damping = 2.0 * zeta * omega;
    const qreal accel = stiffness * error - damping * state.velocity;
    state.velocity += accel * dt;
    state.value += state.velocity * dt;
    state.time += dt;
}

qreal Spring::settleTime() const
{
    // 2% settling for second-order system.
    //
    // - Underdamped (ζ < 1): envelope exp(-ζω·t) = 0.02 dominates
    //   → t = -ln(0.02)/(ζω) ≈ 3.912/(ζω). Ignores the 1/sqrt(1-ζ²)
    //   amplitude factor (which diverges as ζ→1); acceptable because
    //   the critical-band blend below covers the ζ≈1 regime.
    // - Critically damped: (1 + ωt)·exp(-ωt) = 0.02 → ωt ≈ 5.834
    //   (see CriticalSettleFactor).
    // - Overdamped (ζ > 1): slow pole p₁ = ω(ζ - √(ζ²-1)) dominates:
    //   t ≈ -ln(0.02)/p₁.
    //
    // Continuity: the formulas step-change at ζ = 1 ± ε by up to ~30%.
    // To avoid visible jumps as the user live-edits ζ near 1.0 (which
    // would jitter repaint budgets), the ε-wide band around critical
    // linearly blends between the two regime formulas at the band edges.
    constexpr qreal target = 0.02;
    const qreal lnTarget = -qLn(target); // ≈ 3.912
    const qreal safeOmega = qMax(1.0e-3, omega);

    auto underdampedSettle = [&](qreal zetaVal) {
        return lnTarget / qMax(1.0e-3, zetaVal * safeOmega);
    };
    auto overdampedSettle = [&](qreal zetaVal) {
        const qreal disc = qSqrt(qMax(0.0, zetaVal * zetaVal - 1.0));
        const qreal p1 = safeOmega * (zetaVal - disc);
        return lnTarget / qMax(1.0e-3, p1);
    };
    const qreal criticalSettle = CriticalSettleFactor / safeOmega;

    qreal seconds;
    if (zeta < 1.0 - CriticalDampingEpsilon) {
        seconds = underdampedSettle(zeta);
    } else if (zeta > 1.0 + CriticalDampingEpsilon) {
        seconds = overdampedSettle(zeta);
    } else {
        // Critical band: quadratic Bézier-ish blend (underdamped → critical →
        // overdamped) pinned to the regime formulas at the band edges and
        // exactly to the critical 2% settle at ζ = 1. Keeps the function
        // C0-continuous and close to the physically correct value.
        const qreal zLow = 1.0 - CriticalDampingEpsilon;
        const qreal zHigh = 1.0 + CriticalDampingEpsilon;
        const qreal lower = underdampedSettle(zLow);
        const qreal upper = overdampedSettle(zHigh);
        if (zeta <= 1.0) {
            // Blend from lower (at zLow) to criticalSettle (at 1.0).
            const qreal alpha = (zeta - zLow) / CriticalDampingEpsilon;
            seconds = lower + (criticalSettle - lower) * alpha;
        } else {
            // Blend from criticalSettle (at 1.0) to upper (at zHigh).
            const qreal alpha = (zeta - 1.0) / CriticalDampingEpsilon;
            seconds = criticalSettle + (upper - criticalSettle) * alpha;
        }
    }
    return qMin(MaxSettleSeconds, qMax(0.001, seconds));
}

QString Spring::toString() const
{
    return QStringLiteral("spring:%1,%2").arg(omega, 0, 'f', 2).arg(zeta, 0, 'f', 2);
}

std::unique_ptr<Curve> Spring::clone() const
{
    return std::make_unique<Spring>(*this);
}

bool Spring::overshoots() const
{
    // Underdamped springs oscillate past the target; critical and
    // overdamped springs approach monotonically from below.
    return zeta < 1.0 - CriticalDampingEpsilon;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parsing + equality + presets
// ═══════════════════════════════════════════════════════════════════════════════

Spring Spring::fromString(const QString& str)
{
    Spring spring;
    if (str.isEmpty())
        return spring;

    const int colonIdx = str.indexOf(QLatin1Char(':'));
    const QString params = (colonIdx >= 0) ? str.mid(colonIdx + 1).trimmed() : str.trimmed();
    if (colonIdx >= 0) {
        const QString prefix = str.left(colonIdx).trimmed();
        if (prefix != QLatin1String("spring")) {
            qCDebug(lcSpring) << "expected 'spring:' prefix, got" << prefix << "- using default";
            return spring;
        }
    }

    const QStringList parts = params.split(QLatin1Char(','));
    if (parts.size() != 2) {
        qCDebug(lcSpring) << "invalid format (expected omega,zeta):" << str << "- using default";
        return spring;
    }

    const QLocale cLocale = QLocale::c();
    bool okOmega, okZeta;
    qreal omegaVal = cLocale.toDouble(parts[0].trimmed(), &okOmega);
    qreal zetaVal = cLocale.toDouble(parts[1].trimmed(), &okZeta);
    if (!okOmega || !okZeta) {
        qCDebug(lcSpring) << "invalid numeric values in" << str << "- using default";
        return spring;
    }

    spring.omega = qBound(0.1, omegaVal, 200.0);
    spring.zeta = qBound(0.0, zetaVal, 10.0);
    return spring;
}

Spring Spring::snappy()
{
    return Spring(12.0, 0.8);
}

Spring Spring::smooth()
{
    return Spring(8.0, 1.0);
}

Spring Spring::bouncy()
{
    return Spring(10.0, 0.5);
}

bool Spring::operator==(const Spring& other) const
{
    return qFuzzyCompare(1.0 + omega, 1.0 + other.omega) && qFuzzyCompare(1.0 + zeta, 1.0 + other.zeta);
}

bool Spring::equals(const Curve& other) const
{
    // Delegate to operator== so polymorphic equality matches the
    // value-type comparison exactly (no drift from lossy toString()).
    if (typeId() != other.typeId()) {
        return false;
    }
    const Spring* rhs = dynamic_cast<const Spring*>(&other);
    if (!rhs) {
        return false;
    }
    return *this == *rhs;
}

} // namespace PhosphorAnimation
