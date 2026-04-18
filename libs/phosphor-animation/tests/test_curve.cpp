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

    void testPolymorphicEqualsIsTight()
    {
        // Parameters below the 2-decimal toString() precision must not
        // be smoothed by Curve::equals() — Easing and Spring override
        // it to delegate to the tight operator==.
        Easing a;
        a.type = Easing::Type::ElasticOut;
        a.amplitude = 1.003;
        Easing b;
        b.type = Easing::Type::ElasticOut;
        b.amplitude = 1.006;
        QVERIFY(!a.equals(b));

        Spring s1(12.001, 0.8);
        Spring s2(12.005, 0.8);
        QVERIFY(!s1.equals(s2));
    }

    void testDefaultStepForStatelessCurve()
    {
        // Curve::step() default: state.value = lerp(startValue, target,
        // evaluate(t)). Start at 0, target 1 — after a full run, value
        // should have traversed from 0 to ~1.
        Easing e;
        CurveState state;
        const qreal dt = 1.0 / 60.0;
        const qreal target = 1.0;

        for (int i = 0; i < 60; ++i) {
            e.step(dt, state, target);
        }
        QVERIFY(qAbs(state.value - 1.0) < 0.05);
    }

    void testDefaultStepHonorsStartValueForRetarget()
    {
        // Retarget mid-flight: set startValue=current, reset time, pick
        // new target. The next step MUST produce continuous motion
        // starting from state.value, not jump to evaluate(t)*newTarget.
        Easing e;
        CurveState state;
        state.value = 0.5;
        state.startValue = 0.5;
        state.time = 0.0;

        // Take one small step toward 2.0. The default step lerps from
        // startValue (0.5) to target (2.0) via evaluate(tiny) ≈ 0, so
        // the new value should be very close to 0.5 — NOT a jump.
        e.step(1.0 / 120.0, state, 2.0);
        QVERIFY(qAbs(state.value - 0.5) < 0.1);
    }

    void testDefaultStepLerpArrivesAtTarget()
    {
        // After a full animation (state.time reaches 1.0), state.value
        // reaches target regardless of startValue.
        Easing e;
        CurveState state;
        state.value = 0.3;
        state.startValue = 0.3;
        const qreal target = 0.9;
        const qreal dt = 1.0 / 60.0;

        for (int i = 0; i < 120; ++i) {
            e.step(dt, state, target);
        }
        QVERIFY(qAbs(state.value - target) < 0.01);
    }

    void testCurveStateDefaults()
    {
        CurveState s;
        QCOMPARE(s.value, 0.0);
        QCOMPARE(s.velocity, 0.0);
        QCOMPARE(s.time, 0.0);
        QCOMPARE(s.startValue, 0.0);
    }

    void testMultipleRefsShareCurve()
    {
        auto s1 = std::make_shared<const Spring>(Spring::snappy());
        std::shared_ptr<const Curve> ref1 = s1;
        std::shared_ptr<const Curve> ref2 = s1;
        QCOMPARE(ref1.use_count(), 3);
        QVERIFY(ref1.get() == ref2.get());
    }
};

QTEST_MAIN(TestCurve)
#include "test_curve.moc"
