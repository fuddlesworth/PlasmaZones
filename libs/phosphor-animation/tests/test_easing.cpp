// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Easing.h>

#include <QTest>
#include <QVector>

using PhosphorAnimation::Easing;

class TestEasing : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── fromString ───

    void testFromStringBezierLegacyBare()
    {
        Easing curve = Easing::fromString(QStringLiteral("0.33,1.00,0.68,1.00"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
        QVERIFY(qAbs(curve.x1 - 0.33) < 0.01);
        QVERIFY(qAbs(curve.y1 - 1.00) < 0.01);
        QVERIFY(qAbs(curve.x2 - 0.68) < 0.01);
        QVERIFY(qAbs(curve.y2 - 1.00) < 0.01);
    }

    void testFromStringBezierPrefixed()
    {
        Easing curve = Easing::fromString(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        QCOMPARE(curve.type, Easing::Type::CubicBezier);
        QVERIFY(qAbs(curve.x1 - 0.25) < 0.01);
        QVERIFY(qAbs(curve.y1 - 0.10) < 0.01);
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
        // amplitude [0.5, 3.0], period [0.1, 1.0]
        Easing curve = Easing::fromString(QStringLiteral("elastic-out:10.0,5.0"));
        QVERIFY(qAbs(curve.amplitude - 3.0) < 0.01);
        QVERIFY(qAbs(curve.period - 1.0) < 0.01);
    }

    void testFromStringClampsBounceRanges()
    {
        // amplitude [0.5, 3.0], bounces [1, 8]
        Easing curve = Easing::fromString(QStringLiteral("bounce-in:0.1,20"));
        QVERIFY(qAbs(curve.amplitude - 0.5) < 0.01);
        QCOMPARE(curve.bounces, 8);
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
            {Easing::Type::ElasticInOut, 0, 0, 0, 0, 0.8, 0.6, 3},
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
};

QTEST_MAIN(TestEasing)
#include "test_easing.moc"
