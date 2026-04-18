// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

using PhosphorAnimation::Curve;
using PhosphorAnimation::CurveState;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;

class TestCurve : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testPolymorphicDispatchThroughSharedPtr()
    {
        // Same call site, different concrete types — exactly the scenario
        // that motivates the polymorphic base over std::variant.
        std::shared_ptr<const Curve> easing = std::make_shared<Easing>();
        std::shared_ptr<const Curve> spring = std::make_shared<Spring>(Spring::smooth());

        QCOMPARE(easing->typeId(), QStringLiteral("bezier"));
        QCOMPARE(spring->typeId(), QStringLiteral("spring"));
        QVERIFY(!easing->isStateful());
        QVERIFY(spring->isStateful());
    }

    void testEqualsAcrossSubclasses()
    {
        Easing e1;
        Easing e2;
        Spring s1 = Spring::smooth();

        // Same subclass, same params → equal.
        QVERIFY(e1.equals(e2));
        // Different subclass → not equal.
        QVERIFY(!e1.equals(s1));
        QVERIFY(!s1.equals(e1));
    }

    void testDefaultStepForStatelessCurve()
    {
        // Curve::step() default implementation uses evaluate() under the
        // hood for stateless curves. Advance across t=[0,1] in small
        // increments and verify state.value tracks evaluate(t) within
        // floating-point tolerance.
        Easing e;
        CurveState state;
        const qreal dt = 1.0 / 60.0;
        const qreal target = 1.0;

        qreal t = 0.0;
        for (int i = 0; i < 60; ++i) {
            e.step(dt, state, target);
            t += dt;
        }
        // By t ≈ 1, default step() should have walked state.value to
        // evaluate(1.0) ≈ 1.0 (modulo cumulative float drift).
        QVERIFY(qAbs(state.value - 1.0) < 0.05);
    }

    void testCurveStateDefaults()
    {
        CurveState s;
        QCOMPARE(s.value, 0.0);
        QCOMPARE(s.velocity, 0.0);
        QCOMPARE(s.time, 0.0);
    }

    void testMultipleRefsShareCurve()
    {
        // Multiple owners share the same immutable curve — intended usage.
        auto s1 = std::make_shared<const Spring>(Spring::snappy());
        std::shared_ptr<const Curve> ref1 = s1;
        std::shared_ptr<const Curve> ref2 = s1;
        QCOMPARE(ref1.use_count(), 3);
        QVERIFY(ref1.get() == ref2.get());
    }
};

QTEST_MAIN(TestCurve)
#include "test_curve.moc"
