// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Spring.h>

#include <QTest>

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
        // Even pathological parameters produce a finite settle time.
        QVERIFY(weak.settleTime() < 31.0);
        QVERIFY(weak.settleTime() > 0.0);
    }

    void testSettleTimeOrder()
    {
        // Stiffer spring settles faster (holding zeta constant).
        Spring stiff(30.0, 1.0);
        Spring loose(4.0, 1.0);
        QVERIFY(stiff.settleTime() < loose.settleTime());
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
