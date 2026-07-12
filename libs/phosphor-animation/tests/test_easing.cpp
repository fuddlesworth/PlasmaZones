// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>

#include <QTest>
#include <QVector>

using PhosphorAnimation::Easing;

class TestEasing : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── fromString ───

    void testFromStringBezierBareWireFormat()
    {
        // Bare 4-comma form is the canonical cubic-bezier wire format
        // — what Easing::toString emits and what configs/QML write.
        Easing curve = Easing::fromString(QStringLiteral("0.33,1.00,0.68,1.00"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
        QVERIFY(qAbs(curve.x1 - 0.33) < 0.01);
        QVERIFY(qAbs(curve.y1 - 1.00) < 0.01);
        QVERIFY(qAbs(curve.x2 - 0.68) < 0.01);
        QVERIFY(qAbs(curve.y2 - 1.00) < 0.01);
    }

    void testFromStringBezierPrefixedRejected()
    {
        // The "bezier:..." prefixed form is intentionally NOT accepted
        // — there is exactly one wire format per curve type. Unknown
        // named-curve falls back to the default OutCubic bezier.
        Easing curve = Easing::fromString(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        Easing def;
        QCOMPARE(curve, def);
    }

    void testFromStringElasticOut()
    {
        Easing curve = Easing::fromString(QStringLiteral("elastic-out:1.0,0.3"));
        QCOMPARE(curve.type, Easing::Type::ElasticOut);
        QVERIFY(qAbs(curve.amplitude - 1.0) < 0.01);
        QVERIFY(qAbs(curve.period - 0.3) < 0.01);
    }

    void testFromStringBounceOut()
    {
        Easing curve = Easing::fromString(QStringLiteral("bounce-out:1.5,4"));
        QCOMPARE(curve.type, Easing::Type::BounceOut);
        QVERIFY(qAbs(curve.amplitude - 1.5) < 0.01);
        QCOMPARE(curve.bounces, 4);
    }

    void testFromStringEmpty()
    {
        Easing curve = Easing::fromString(QString());
        QCOMPARE(curve.type, Easing::Type::CubicBezier); // default
    }

    void testFromStringInvalidNameFallsBack()
    {
        Easing curve = Easing::fromString(QStringLiteral("bogus:1.0,2.0"));
        // Unknown named curve falls back to default (CubicBezier outCubic).
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
    }

    void testFromStringBezierPartialParamsLogsAndFallsBack()
    {
        // "bezier:0.5,0.5" is only 2 of 4 params — must fall back to the
        // default curve rather than silently accepting a half-configured one.
        Easing curve = Easing::fromString(QStringLiteral("bezier:0.5,0.5"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
        // Defaults: x1=0.33, y1=1.0, x2=0.68, y2=1.0
        QVERIFY(qAbs(curve.x1 - 0.33) < 0.01);
        QVERIFY(qAbs(curve.y1 - 1.00) < 0.01);
    }

    void testFromStringScientificNotNamed()
    {
        // Numeric strings with 'e' exponent must NOT be misclassified as named.
        Easing curve = Easing::fromString(QStringLiteral("1.5e-3,0.5,0.7,1.0"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
    }

    void testFromStringClampsBezierRanges()
    {
        // x clamped [0,1], y clamped [-1, 2].
        Easing curve = Easing::fromString(QStringLiteral("-5.0,3.0,5.0,-5.0"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
        QVERIFY(qAbs(curve.x1 - 0.0) < 0.01);
        QVERIFY(qAbs(curve.y1 - 2.0) < 0.01);
        QVERIFY(qAbs(curve.x2 - 1.0) < 0.01);
        QVERIFY(qAbs(curve.y2 - -1.0) < 0.01);
    }

    void testFromStringClampsElasticRanges()
    {
        // Elastic amplitude [1.0, 2.0] (see Easing::clampAmplitude), period [0.1, 1.0].
        Easing curve = Easing::fromString(QStringLiteral("elastic-out:10.0,5.0"));
        QVERIFY(qAbs(curve.amplitude - 2.0) < 0.01);
        QVERIFY(qAbs(curve.period - 1.0) < 0.01);

        // The floor is 1.0, not bounce's 0.5: below 1.0 the curve's own asin(1/a)
        // term made every value behave exactly like 1.0 anyway.
        Easing floored = Easing::fromString(QStringLiteral("elastic-in:0.1,0.01"));
        QVERIFY(qAbs(floored.amplitude - 1.0) < 0.01);
        QVERIFY(qAbs(floored.period - 0.1) < 0.01);
    }

    void testFromStringClampsBounceRanges()
    {
        // Bounce keeps the wider amplitude [0.5, 3.0] — it never leaves [0, 1], so
        // the overshoot envelope has no claim on it. bounces [1, 8].
        Easing curve = Easing::fromString(QStringLiteral("bounce-in:0.1,20"));
        QVERIFY(qAbs(curve.amplitude - 0.5) < 0.01);
        QCOMPARE(curve.bounces, 8);

        Easing tall = Easing::fromString(QStringLiteral("bounce-out:3.0,4"));
        QVERIFY(qAbs(tall.amplitude - 3.0) < 0.01);
    }

    // ─── Round-trip ───

    void testRoundtripAllVariants()
    {
        struct Case
        {
            Easing::Type type;
            qreal x1, y1, x2, y2;
            qreal amplitude;
            qreal period;
            int bounces;
        };

        const QVector<Case> cases = {
            {Easing::Type::CubicBezier, 0.25, 0.10, 0.25, 1.00, 1.0, 0.3, 3},
            {Easing::Type::ElasticOut, 0, 0, 0, 0, 1.2, 0.4, 3},
            {Easing::Type::ElasticIn, 0, 0, 0, 0, 1.5, 0.5, 3},
            // Elastic amplitudes must be inside [1.0, 2.0] or the parse clamps them
            // and the round-trip legitimately fails. 1.8 exercises the top of the
            // range; the old 0.8 sat in what was always a dead zone.
            {Easing::Type::ElasticInOut, 0, 0, 0, 0, 1.8, 0.6, 3},
            {Easing::Type::BounceOut, 0, 0, 0, 0, 1.0, 0.3, 5},
            {Easing::Type::BounceIn, 0, 0, 0, 0, 2.0, 0.3, 3},
            {Easing::Type::BounceInOut, 0, 0, 0, 0, 1.3, 0.3, 6},
        };

        for (const Case& c : cases) {
            Easing original;
            original.type = c.type;
            if (c.type == Easing::Type::CubicBezier) {
                original.x1 = c.x1;
                original.y1 = c.y1;
                original.x2 = c.x2;
                original.y2 = c.y2;
            } else {
                original.amplitude = c.amplitude;
                original.period = c.period;
                original.bounces = c.bounces;
            }
            const QString encoded = original.toString();
            const Easing restored = Easing::fromString(encoded);
            QCOMPARE(restored, original);
        }
    }

    // ─── evaluate ───

    void testEvaluateBoundaries()
    {
        // t=0 → ~0 and t=1 → ~1 for every curve type.
        QVector<Easing> curves;
        for (int t = 0; t < 7; ++t) {
            Easing e;
            e.type = static_cast<Easing::Type>(t);
            curves.append(e);
        }
        for (const Easing& e : curves) {
            QVERIFY(qAbs(e.evaluate(0.0)) < 0.01);
            QVERIFY(qAbs(e.evaluate(1.0) - 1.0) < 0.01);
        }
    }

    void testElasticOvershoot()
    {
        // Elastic out should overshoot 1.0 at some mid-t sample.
        Easing elastic;
        elastic.type = Easing::Type::ElasticOut;
        elastic.amplitude = 1.0;
        elastic.period = 0.3;

        bool foundOvershoot = false;
        for (int i = 1; i < 100; ++i) {
            const qreal t = qreal(i) / 100.0;
            if (elastic.evaluate(t) > 1.0) {
                foundOvershoot = true;
                break;
            }
        }
        QVERIFY(foundOvershoot);
    }

    void testElasticUndershoot()
    {
        // At high amplitude + short period, elastic-out dips well below
        // zero during early oscillation. Consumers relying on the
        // "may overshoot [0, 1]" contract must not clamp this.
        Easing elastic;
        elastic.type = Easing::Type::ElasticOut;
        elastic.amplitude = 3.0;
        elastic.period = 0.1;

        bool foundUndershoot = false;
        for (int i = 1; i < 100; ++i) {
            const qreal t = qreal(i) / 100.0;
            if (elastic.evaluate(t) < 0.0) {
                foundUndershoot = true;
                break;
            }
        }
        QVERIFY(foundUndershoot);
    }

    // ─── Curve virtuals ───

    void testTypeIdMatchesSerialization()
    {
        Easing e;
        e.type = Easing::Type::ElasticOut;
        QCOMPARE(e.typeId(), QStringLiteral("elastic-out"));
        QVERIFY(e.toString().startsWith(QStringLiteral("elastic-out:")));
    }

    void testCloneProducesIndependentEqualInstance()
    {
        Easing original;
        original.type = Easing::Type::BounceIn;
        original.amplitude = 1.7;
        original.bounces = 5;

        std::unique_ptr<PhosphorAnimation::Curve> cloned = original.clone();
        QVERIFY(cloned != nullptr);
        QVERIFY(cloned->equals(original));
        // Mutating the clone is possible via subclass-specific pointer,
        // but the polymorphic Curve API is immutable — we verify that
        // toString() round-trips identically across clone().
        QCOMPARE(cloned->toString(), original.toString());
    }

    void testIsStatefulFalse()
    {
        Easing e;
        QVERIFY(!e.isStateful());
    }

    void testSettleTimeIsUnit()
    {
        Easing e;
        QVERIFY(qAbs(e.settleTime() - 1.0) < 1e-9);
    }

    // ─── amplitude bounds ───

    // Elastic and bounce share the `amplitude` field but not its meaning, so they
    // must not share its bounds. The clamp was type-blind and applied elastic's
    // ceiling to bounce and bounce's floor to elastic.
    void testClampAmplitudeIsTypeAware()
    {
        // Elastic: the overshoot envelope caps it, and asin(1/a) floors it at 1.
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 9.0), 2.0);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticIn, 9.0), 2.0);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticInOut, 0.1), 1.0);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 1.5), 1.5); // in range, untouched

        // Bounce never leaves [0, 1], so the envelope has no claim on it and its
        // wider range stays open — the value scales dip depth, and all of it is live.
        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 3.0), 3.0);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 0.5), 0.5);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 9.0), 3.0);
    }

    // The old floor of 0.5 advertised a range that did nothing: evaluateElasticOut
    // floored the amplitude at 1.0 internally, so the entire lower half of the
    // slider produced a curve identical to amplitude 1.0. The floor now states what
    // the arithmetic already enforced.
    void testElasticAmplitudeBelowOneWasADeadZone()
    {
        Easing at1 = Easing::fromString(QStringLiteral("elastic-out:1.0,0.3"));
        Easing atHalf = Easing::fromString(QStringLiteral("elastic-out:0.5,0.3"));
        QCOMPARE(atHalf.amplitude, 1.0); // clamped up, not stored as 0.5
        for (int i = 0; i <= 20; ++i) {
            const qreal t = qreal(i) / 20.0;
            QVERIFY(qAbs(at1.evaluate(t) - atHalf.evaluate(t)) < 1.0e-12);
        }
    }

    // What the amplitude ceiling actually buys. It does NOT bring elastic inside
    // the overshoot envelope — elastic-out still grazes past the top and elastic-in
    // past the bottom, and the consumers clip that (briefly, by design). What it
    // does is BOUND the excursion, so the clip stays small: at the old cap of 3.0
    // the curve reached ~3.44 / ~-2.44, which is a window flung far enough that
    // clipping it would be a visible flat spot rather than a fraction of a frame.
    //
    // Pinned as a range with headroom, not an exact peak: the point is that the
    // excursion is bounded and close to the envelope, and an exact figure would
    // just re-encode the formula. If this fails, the amplitude cap moved.
    void testAmplitudeCeilingBoundsTheElasticExcursion()
    {
        const QVector<Easing::Type> types{Easing::Type::ElasticIn, Easing::Type::ElasticOut,
                                          Easing::Type::ElasticInOut};
        for (const Easing::Type type : types) {
            for (int ai = 0; ai <= 20; ++ai) {
                for (int pi = 0; pi <= 18; ++pi) {
                    Easing e;
                    e.type = type;
                    // Sweep PAST the admitted range on the amplitude axis: the clamp
                    // inside evaluate() is what has to hold, since `Easing` is a POD
                    // whose fields any caller can write directly.
                    e.amplitude = Easing::clampAmplitude(type, 0.5 + 0.15 * ai); // 0.5 .. 3.5
                    e.period = qBound(0.1, 0.1 + 0.05 * pi, 1.0);
                    for (int i = 0; i <= 200; ++i) {
                        const qreal t = qreal(i) / 200.0;
                        const qreal v = e.evaluate(t);
                        QVERIFY2(v >= -1.7 && v <= 2.7,
                                 qPrintable(QStringLiteral("elastic excursion is unbounded: amp=%1 per=%2 "
                                                           "t=%3 -> %4")
                                                .arg(e.amplitude)
                                                .arg(e.period)
                                                .arg(t)
                                                .arg(v)));
                        // ...and whatever it produced, a consumer bounds it into the
                        // envelope before interpolating with it.
                        const qreal bounded = PhosphorAnimation::boundCurveProgress(v);
                        QVERIFY(bounded >= PhosphorAnimation::Limits::MinCurveProgress
                                && bounded <= PhosphorAnimation::Limits::MaxCurveProgress);
                    }
                }
            }
        }
    }

    // A direct field write bypasses both the parse clamp and the property setter —
    // `Easing` is a POD with public fields — so evaluate() has to bound it itself,
    // or a hand-edited profile reaches the geometry lerp with amplitude 50.
    void testElasticBoundsAPokedAmplitude()
    {
        Easing poked;
        poked.type = Easing::Type::ElasticOut;
        poked.period = 0.1; // the worst corner
        poked.amplitude = 50.0; // never survives a clamp, but nothing stops a caller

        Easing capped = poked;
        capped.amplitude = 2.0; // what the clamp would have produced

        for (int i = 0; i <= 200; ++i) {
            const qreal t = qreal(i) / 200.0;
            QCOMPARE(poked.evaluate(t), capped.evaluate(t));
        }
    }
};

QTEST_MAIN(TestEasing)
#include "test_easing.moc"
