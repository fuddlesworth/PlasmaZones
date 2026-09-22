// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Spring.h>

#include <QTest>

#include <QtMath>

#include <cmath>
#include <limits>

using PhosphorAnimation::CurveState;
using PhosphorAnimation::Spring;

class TestSpring : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Parameters & parsing ───

    void testDefaultConstruction()
    {
        Spring s;
        QCOMPARE(s.typeId(), QStringLiteral("spring"));
        QVERIFY(s.omega > 0.0);
        QVERIFY(s.zeta > 0.0);
        QVERIFY(s.isStateful());
    }

    void testExplicitConstructionClamps()
    {
        Spring s(500.0, -1.0);
        // omega clamped [0.1, 200], zeta clamped [0, 10]
        QVERIFY(qAbs(s.omega - 200.0) < 1e-6);
        QVERIFY(qAbs(s.zeta - 0.0) < 1e-6);
    }

    void testFromStringWithPrefix()
    {
        Spring s = Spring::fromString(QStringLiteral("spring:12.50,0.80"));
        QVERIFY(qAbs(s.omega - 12.5) < 0.01);
        QVERIFY(qAbs(s.zeta - 0.8) < 0.01);
    }

    void testFromStringWithoutPrefix()
    {
        Spring s = Spring::fromString(QStringLiteral("8.0,1.0"));
        QVERIFY(qAbs(s.omega - 8.0) < 0.01);
        QVERIFY(qAbs(s.zeta - 1.0) < 0.01);
    }

    void testFromStringInvalidFallsBackToDefault()
    {
        Spring s = Spring::fromString(QStringLiteral("bogus"));
        Spring def;
        QCOMPARE(s, def);
    }

    void testFromStringRoundTrip()
    {
        const Spring original(15.3, 0.65);
        const Spring restored = Spring::fromString(original.toString());
        QCOMPARE(restored, original);
    }

    // ─── Presets ───

    void testPresets()
    {
        // Snappy: slightly underdamped (has some bounce but settles quickly)
        const Spring snap = Spring::snappy();
        QVERIFY(snap.zeta < 1.0);
        QVERIFY(snap.zeta > 0.5);

        // Smooth: critically damped, no oscillation
        const Spring smooth = Spring::smooth();
        QVERIFY(qAbs(smooth.zeta - 1.0) < 0.05);

        // Bouncy: visible oscillation
        const Spring bouncy = Spring::bouncy();
        QVERIFY(bouncy.zeta < 0.7);
    }

    // ─── evaluate ───

    void testEvaluateBoundaries()
    {
        Spring s = Spring::smooth();
        // t=0 starts at 0; t=1 is the settle point, approximately 1.
        QVERIFY(qAbs(s.evaluate(0.0)) < 1e-9);
        const qreal atUnity = s.evaluate(1.0);
        QVERIFY(qAbs(atUnity - 1.0) < 0.05);
    }

    void testEvaluateUnderdampedOvershoots()
    {
        Spring bouncy = Spring::bouncy();
        bool foundOvershoot = false;
        for (int i = 1; i < 100; ++i) {
            const qreal t = qreal(i) / 100.0;
            if (bouncy.evaluate(t) > 1.0) {
                foundOvershoot = true;
                break;
            }
        }
        QVERIFY(foundOvershoot);
    }

    void testEvaluateCriticalNoOvershoot()
    {
        Spring critical(12.0, 1.0);
        for (int i = 0; i <= 100; ++i) {
            const qreal t = qreal(i) / 100.0;
            const qreal v = critical.evaluate(t);
            // Critical damping: never overshoots the target.
            QVERIFY(v <= 1.0 + 1e-3);
        }
    }

    void testEvaluateNearCriticalNumericallyStable()
    {
        // Zeta very close to 1.0 used to route through the underdamped
        // branch where sqrt(1-zeta²) approached 0, giving ill-conditioned
        // division. The critical-damping band is widened to cover this.
        for (qreal zeta : {0.9995, 0.9999, 1.0, 1.0001, 1.0005}) {
            Spring s(12.0, zeta);
            for (int i = 0; i <= 50; ++i) {
                const qreal t = qreal(i) / 50.0;
                const qreal v = s.evaluate(t);
                QVERIFY(qIsFinite(v));
                QVERIFY(v > -2.0 && v < 3.0); // sane magnitude
            }
        }
    }

    void testEvaluateOverdampedNumericallyStable()
    {
        // Just outside the critical band, c1/c2 grow like 1/disc — guard
        // against infinity / NaN at zeta values where disc = √(ζ²-1) is
        // small but nonzero, and confirm the overdamped formula stays
        // monotonic and bounded across the documented zeta range.
        for (qreal zeta : {1.01, 1.05, 1.5, 5.0, 10.0}) {
            Spring s(12.0, zeta);
            qreal prev = 0.0;
            for (int i = 0; i <= 100; ++i) {
                const qreal t = qreal(i) / 100.0;
                const qreal v = s.evaluate(t);
                QVERIFY2(qIsFinite(v), qPrintable(QStringLiteral("non-finite at zeta=%1, t=%2").arg(zeta).arg(t)));
                // Overdamped: monotonic non-decreasing toward target, no overshoot.
                QVERIFY(v >= prev - 1e-6);
                QVERIFY(v <= 1.0 + 1e-3);
                prev = v;
            }
        }
    }

    // ─── step (physics) ───

    void testStepConvergesToTarget()
    {
        Spring s = Spring::smooth();
        CurveState state;
        const qreal target = 1.0;
        // Integrate long enough that critically-damped settles.
        const qreal dt = 1.0 / 120.0;
        const int steps = 600; // 5 seconds
        for (int i = 0; i < steps; ++i) {
            s.step(dt, state, target);
        }
        QVERIFY(qAbs(state.value - target) < 1e-3);
        QVERIFY(qAbs(state.velocity) < 1e-2);
    }

    void testStepRetargetPreservesVelocity()
    {
        // Spring accelerates toward target A; mid-flight we retarget to B.
        // Velocity at the retarget instant must be preserved — otherwise the
        // visual has a "stall" discontinuity.
        Spring s = Spring::snappy();
        CurveState state;
        const qreal dt = 1.0 / 120.0;

        // Phase 1: animate toward 1.0 for 100 ms.
        for (int i = 0; i < 12; ++i) {
            s.step(dt, state, 1.0);
        }
        const qreal velocityBefore = state.velocity;
        QVERIFY(velocityBefore > 0.0);

        // Retarget to -0.5 with a single immediate step. Velocity should
        // not jump to zero — the integrator just receives a different
        // error and accumulates force on top of existing velocity.
        s.step(dt, state, -0.5);
        QVERIFY(qAbs(state.velocity - velocityBefore) < qAbs(velocityBefore) * 0.5);
    }

    void testStepZeroDtIsNoOp()
    {
        Spring s = Spring::snappy();
        CurveState state{0.3, 1.5, 0.1};
        const CurveState before = state;
        s.step(0.0, state, 1.0);
        QCOMPARE(state.value, before.value);
        QCOMPARE(state.velocity, before.velocity);
    }

    void testStepConvergenceLocks()
    {
        // Once converged, further steps do not accumulate drift.
        Spring s = Spring::smooth();
        CurveState state;
        for (int i = 0; i < 5000; ++i) {
            s.step(1.0 / 120.0, state, 1.0);
        }
        const qreal settled = state.value;
        // Run another 10 000 steps — value must not drift by more than
        // the convergence epsilon.
        for (int i = 0; i < 10000; ++i) {
            s.step(1.0 / 120.0, state, 1.0);
        }
        QVERIFY(qAbs(state.value - settled) < 1e-3);
    }

    // ─── settleTime ───

    void testSettleTimeBounded()
    {
        Spring weak(0.2, 0.05);
        // Even pathological parameters produce a finite settle time, bounded by
        // the class's own published ceiling rather than a magic number.
        QVERIFY(weak.settleTime() <= Spring::MaxSettleSeconds);
        QVERIFY(weak.settleTime() > 0.0);

        // The extreme: an UNDAMPED spring never settles analytically, so it must
        // land exactly on the ceiling rather than running away.
        Spring undamped(12.0, 0.0);
        QCOMPARE(undamped.settleTime(), Spring::MaxSettleSeconds);
    }

    void testSettleTimeOrder()
    {
        // Stiffer spring settles faster (holding zeta constant).
        Spring stiff(30.0, 1.0);
        Spring loose(4.0, 1.0);
        QVERIFY(stiff.settleTime() < loose.settleTime());
    }

    // settleTime() IS the animation's lifetime, so the residual at t == 1 is the
    // terminal jump the user sees when the animation ends and the window snaps to
    // its final rect. That residual must be bounded by the settling band across
    // EVERY damping regime — which requires settleTime() to bound the amplitude as
    // well as the decay. Bounding the bare exponential (the classic shortcut) left
    // 2.8% at zeta = 0.99, worse than the 2% band it was tightened to escape,
    // because the step response's amplitude coefficient diverges as zeta → 1 from
    // either side.
    //
    // This sweep is the only thing that fails if the band is loosened OR if the
    // amplitude term is dropped again. Without it the whole change is untested.
    void testSettleTimeBoundsTheResidualAcrossAllRegimes()
    {
        constexpr qreal kBand = 0.005;
        const QList<qreal> zetas = {0.3, 0.5, 0.8, 0.95, 0.99, 1.0, 1.0011, 1.05, 1.5, 3.0};
        for (const qreal z : zetas) {
            const Spring s(12.0, z);
            const qreal residual = qAbs(1.0 - s.evaluate(1.0));
            QVERIFY2(residual <= kBand + 1.0e-6,
                     qPrintable(QStringLiteral("zeta=%1 leaves a %2%% residual at t=1, outside the %3%% band")
                                    .arg(z)
                                    .arg(residual * 100.0)
                                    .arg(kBand * 100.0)));
        }
    }

    // Pin the critical coefficient itself: it is the numerical root of
    // (1 + x)·exp(-x) = SettleBand and has no closed form, so a band change that
    // forgets to re-solve it would otherwise pass silently.
    void testCriticalSettleFactorMatchesTheBand()
    {
        const Spring critical(10.0, 1.0);
        // omega * settleTime == CriticalSettleFactor == 7.4301 for a 0.5% band.
        const qreal omegaT = 10.0 * critical.settleTime();
        QVERIFY(qAbs(omegaT - 7.4301) < 1.0e-3);
        // ...and that really is the root: (1 + x)·exp(-x) lands on the band.
        QVERIFY(qAbs((1.0 + omegaT) * qExp(-omegaT) - 0.005) < 1.0e-5);
    }

    // ─── Integrator divergence backstop ───

    // The exact integrator cannot diverge — every term is a decaying exponential — so
    // the guard in step() is a NET for a state that arrives already poisoned (a
    // non-finite value lerped in from upstream), not a fence against instability.
    // Pin that: a garbage input must fail to the target rather than propagate, because
    // propagating it puts a garbage rect on screen and a NaN in a shader uniform.
    void testStepSnapsToTargetOnAPoisonedState()
    {
        const Spring s = Spring::snappy();
        for (const qreal poison : {std::numeric_limits<qreal>::infinity(), -std::numeric_limits<qreal>::infinity(),
                                   std::numeric_limits<qreal>::quiet_NaN()}) {
            CurveState state;
            state.value = poison;
            s.step(1.0 / 60.0, state, 1.0);
            QVERIFY2(std::isfinite(state.value), "a poisoned value must not propagate");
            QCOMPARE(state.value, 1.0);
            QCOMPARE(state.velocity, 0.0);
        }
    }

    // The backstop must not fire for a spring that is merely underdamped: a normal
    // bouncy spring at a normal frame delta overshoots 1.0 and must keep doing so.
    void testStepPreservesLegitimateOvershoot()
    {
        const Spring bouncy(12.0, 0.5);
        CurveState state;
        qreal peak = 0.0;
        for (int i = 0; i < 120; ++i) {
            bouncy.step(0.016, state, 1.0);
            QVERIFY(std::isfinite(state.value));
            peak = qMax(peak, state.value);
        }
        QVERIFY2(peak > 1.0, "an underdamped spring must still overshoot — the backstop must not clamp it");
    }

    // ─── Cloning & equality ───

    void testClone()
    {
        Spring original(10.0, 0.7);
        std::unique_ptr<PhosphorAnimation::Curve> cloned = original.clone();
        QVERIFY(cloned != nullptr);
        QVERIFY(cloned->equals(original));
        QCOMPARE(cloned->toString(), original.toString());
    }
};

QTEST_MAIN(TestSpring)
#include "test_spring.moc"
