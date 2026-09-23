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
        // 1.5 sits inside the reachable range at period 0.3 (whose floor is ~1.37),
        // so this exercises the parse rather than the clamp — the clamp has its own
        // tests below.
        Easing curve = Easing::fromString(QStringLiteral("elastic-out:1.5,0.3"));
        QCOMPARE(curve.type, Easing::Type::ElasticOut);
        QVERIFY(qAbs(curve.amplitude - 1.5) < 0.01);
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
        // Elastic amplitude is the PEAK, capped at the overshoot envelope; period
        // [0.1, 1.0]. See Easing::clampAmplitude.
        Easing curve = Easing::fromString(QStringLiteral("elastic-out:10.0,5.0"));
        QVERIFY(qAbs(curve.amplitude - Easing::MaxElasticPeak) < 0.01);
        QVERIFY(qAbs(curve.period - 1.0) < 0.01);

        // The floor is whatever peak the period can actually reach — not a constant,
        // and not bounce's 0.5. At period 0.1 the curve cannot bounce gentler than
        // ~1.71 no matter what is asked of it.
        Easing floored = Easing::fromString(QStringLiteral("elastic-in:0.1,0.01"));
        QVERIFY(qAbs(floored.period - 0.1) < 0.01);
        QVERIFY(qAbs(floored.amplitude - Easing::minElasticPeak(0.1)) < 0.01);
        QVERIFY(floored.amplitude > 1.7);
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
            // An elastic amplitude must sit at or above minElasticPeak(period) or the
            // parse clamps it up to the floor and the round-trip legitimately fails.
            // The floors here are ~1.28 (period 0.4), ~1.20 (0.5) and ~1.15 (0.6).
            {Easing::Type::ElasticOut, 0, 0, 0, 0, 1.4, 0.4, 3},
            {Easing::Type::ElasticIn, 0, 0, 0, 0, 1.5, 0.5, 3},
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
        // Elastic-IN winds up BACKWARD before it travels, dipping well below zero.
        // Consumers relying on the "may leave [0, 1]" contract must not clamp this.
        //
        // This used to be asserted of elastic-OUT, which no longer undershoots at
        // all: that only ever happened at an amplitude whose peak (3.44) is outside
        // the overshoot envelope and is no longer admitted. Elastic-out now spans
        // exactly [0, 2] and elastic-in exactly [-1, 1], so elastic-in is where the
        // sub-zero half of the contract actually lives.
        Easing elastic;
        elastic.type = Easing::Type::ElasticIn;
        elastic.amplitude = Easing::MaxElasticPeak;
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

    // ─── amplitude = the peak (reparametrised) ───

    // THE contract. Elastic's amplitude is the peak the curve reaches, so asking for
    // 1.5 must produce a curve whose maximum is 1.5 — at EVERY period. Under the old
    // Penner parameterisation the same amplitude spanned a ~5x range of real overshoot
    // depending on where the period sat, which is the wart this replaced.
    void testElasticAmplitudeIsTheActualPeak()
    {
        for (int pi = 0; pi <= 18; ++pi) {
            const qreal period = qBound(0.1, 0.1 + 0.05 * pi, 1.0);
            const qreal floorPeak = Easing::minElasticPeak(period);
            for (int k = 0; k <= 10; ++k) {
                const qreal wanted = floorPeak + (Easing::MaxElasticPeak - floorPeak) * k / 10.0;
                Easing e;
                e.type = Easing::Type::ElasticOut;
                e.period = period;
                e.amplitude = wanted;

                qreal observed = 0.0;
                for (int i = 0; i <= 4000; ++i) {
                    observed = qMax(observed, e.evaluate(qreal(i) / 4000.0));
                }
                QVERIFY2(qAbs(observed - wanted) < 2.0e-3,
                         qPrintable(QStringLiteral("asked for peak %1 at period %2, curve actually peaked at %3")
                                        .arg(wanted)
                                        .arg(period)
                                        .arg(observed)));
            }
        }
    }

    // The payoff: because the amplitude IS the peak and it is capped at the overshoot
    // envelope, elastic can no longer leave the envelope at all. Nothing clips it —
    // the envelope is the curve's own range rather than a bound imposed on it.
    // Elastic-out reaches exactly the top, elastic-in exactly the bottom.
    void testElasticExactlySpansTheOvershootEnvelope()
    {
        for (int pi = 0; pi <= 18; ++pi) {
            const qreal period = qBound(0.1, 0.1 + 0.05 * pi, 1.0);
            for (const Easing::Type type :
                 {Easing::Type::ElasticIn, Easing::Type::ElasticOut, Easing::Type::ElasticInOut}) {
                Easing e;
                e.type = type;
                e.period = period;
                e.amplitude = Easing::MaxElasticPeak; // the bounciest curve admitted
                for (int i = 0; i <= 4000; ++i) {
                    const qreal v = e.evaluate(qreal(i) / 4000.0);
                    QVERIFY2(v >= PhosphorAnimation::Limits::MinCurveProgress - 1.0e-6
                                 && v <= PhosphorAnimation::Limits::MaxCurveProgress + 1.0e-6,
                             qPrintable(QStringLiteral("elastic left the envelope: period=%1 t=%2 -> %3")
                                            .arg(period)
                                            .arg(qreal(i) / 4000.0)
                                            .arg(v)));
                }
            }
        }

        // ...and it genuinely REACHES both edges — a bound that is never approached
        // would be vacuous, and this is what makes the envelope exact.
        Easing out;
        out.type = Easing::Type::ElasticOut;
        out.amplitude = Easing::MaxElasticPeak;
        Easing in = out;
        in.type = Easing::Type::ElasticIn;
        qreal hi = 0.0;
        qreal lo = 0.0;
        for (int i = 0; i <= 4000; ++i) {
            hi = qMax(hi, out.evaluate(qreal(i) / 4000.0));
            lo = qMin(lo, in.evaluate(qreal(i) / 4000.0));
        }
        QVERIFY(qAbs(hi - PhosphorAnimation::Limits::MaxCurveProgress) < 2.0e-3);
        QVERIFY(qAbs(lo - PhosphorAnimation::Limits::MinCurveProgress) < 2.0e-3);
    }

    // The floor is real and it MOVES with the period: the curve starts a full unit
    // below the target, so its wave cannot begin smaller than that, and at a short
    // period the crest lands before the envelope has decayed. You simply cannot ask
    // elastic for a gentle bounce at period 0.1. The old parameter hid this; the new
    // one reports it, so the UI can show the honest range.
    void testMinElasticPeakRisesAsThePeriodShortens()
    {
        QVERIFY(Easing::minElasticPeak(0.1) > Easing::minElasticPeak(0.3));
        QVERIFY(Easing::minElasticPeak(0.3) > Easing::minElasticPeak(1.0));
        QVERIFY(qAbs(Easing::minElasticPeak(0.1) - 1.711) < 0.01);
        QVERIFY(qAbs(Easing::minElasticPeak(1.0) - 1.053) < 0.01);
        // Every floor is a peak the curve can actually hit, and none escapes the cap.
        for (int pi = 0; pi <= 18; ++pi) {
            const qreal period = qBound(0.1, 0.1 + 0.05 * pi, 1.0);
            const qreal floorPeak = Easing::minElasticPeak(period);
            QVERIFY(floorPeak > 1.0 && floorPeak <= Easing::MaxElasticPeak);
        }
    }

    // Clamping is type-aware AND period-aware. Elastic's range is the curve's own
    // reachable one; bounce never leaves [0, 1] so the envelope has no claim on it
    // and it keeps the wider decorative range, with the period ignored.
    void testClampAmplitudeIsTypeAndPeriodAware()
    {
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 9.0, 0.3), Easing::MaxElasticPeak);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticIn, 9.0, 0.3), Easing::MaxElasticPeak);
        // Below the period's floor, it is raised to the floor — not to a constant.
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 1.0, 0.1), Easing::minElasticPeak(0.1));
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 1.0, 1.0), Easing::minElasticPeak(1.0));
        QVERIFY(Easing::clampAmplitude(Easing::Type::ElasticOut, 1.0, 0.1)
                > Easing::clampAmplitude(Easing::Type::ElasticOut, 1.0, 1.0));
        // A value inside the range survives untouched.
        QCOMPARE(Easing::clampAmplitude(Easing::Type::ElasticOut, 1.9, 0.3), 1.9);

        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 3.0, 0.1), 3.0);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 0.5, 1.0), 0.5);
        QCOMPARE(Easing::clampAmplitude(Easing::Type::BounceOut, 9.0, 0.3), 3.0);
    }

    // The parse path reads amplitude (parts[0]) BEFORE period (parts[1]), but elastic's
    // amplitude floor depends on the period. Clamping in field order would bound it
    // against the default period instead of the one being parsed.
    void testParseClampsAmplitudeAgainstTheParsedPeriodNotTheDefault()
    {
        // 1.2 is below the floor at period 0.1 (1.711) but above it at period 1.0.
        const Easing tight = Easing::fromString(QStringLiteral("elastic-out:1.2,0.1"));
        QVERIFY(qAbs(tight.amplitude - Easing::minElasticPeak(0.1)) < 1.0e-9);
        const Easing loose = Easing::fromString(QStringLiteral("elastic-out:1.2,1.0"));
        QCOMPARE(loose.amplitude, 1.2); // in range at this period — untouched
    }

    // A bare "elastic-out" carries no params, and the struct's default amplitude (1.0)
    // is below EVERY elastic floor. If the parse skipped the clamp in that case it
    // would store a peak the curve cannot produce while evaluate() rendered a
    // different one, so the settings UI would read back a number the compositor is
    // not drawing. Stored must equal used.
    void testParseClampsANamedCurveWithNoParams()
    {
        const Easing bare = Easing::fromString(QStringLiteral("elastic-out"));
        QCOMPARE(bare.type, Easing::Type::ElasticOut);
        QCOMPARE(bare.amplitude, Easing::minElasticPeak(bare.period));

        qreal observed = 0.0;
        for (int i = 0; i <= 4000; ++i) {
            observed = qMax(observed, bare.evaluate(qreal(i) / 4000.0));
        }
        QVERIFY2(qAbs(observed - bare.amplitude) < 2.0e-3,
                 qPrintable(QStringLiteral("stored %1 but rendered %2").arg(bare.amplitude).arg(observed)));
    }

    // The gain solver memoises its last (peak, period). A memo is only legitimate if
    // it is invisible, so interleave two different elastic curves — the access pattern
    // that would serve a stale gain — and require each to match what it produces when
    // evaluated alone.
    void testInterleavedElasticCurvesDoNotShareAStaleGain()
    {
        Easing a;
        a.type = Easing::Type::ElasticOut;
        a.period = 0.2;
        a.amplitude = 1.9;
        Easing b;
        b.type = Easing::Type::ElasticOut;
        b.period = 0.8;
        b.amplitude = 1.2;

        QVector<qreal> aAlone;
        QVector<qreal> bAlone;
        for (int i = 0; i <= 200; ++i) {
            aAlone.append(a.evaluate(qreal(i) / 200.0));
        }
        for (int i = 0; i <= 200; ++i) {
            bAlone.append(b.evaluate(qreal(i) / 200.0));
        }
        for (int i = 0; i <= 200; ++i) {
            const qreal t = qreal(i) / 200.0;
            QCOMPARE(a.evaluate(t), aAlone[i]);
            QCOMPARE(b.evaluate(t), bAlone[i]); // forces a memo miss on every call
        }
    }

    // A direct field write bypasses both the parse clamp and the property setter —
    // `Easing` is a POD with public fields — so evaluate() has to clamp it itself, or
    // a hand-edited profile reaches the geometry lerp with amplitude 50.
    void testElasticClampsAPokedAmplitude()
    {
        Easing poked;
        poked.type = Easing::Type::ElasticOut;
        poked.period = 0.1;
        poked.amplitude = 50.0; // never survives a clamp, but nothing stops a caller

        Easing capped = poked;
        capped.amplitude = Easing::MaxElasticPeak;

        for (int i = 0; i <= 200; ++i) {
            const qreal t = qreal(i) / 200.0;
            QCOMPARE(poked.evaluate(t), capped.evaluate(t));
        }
    }
};

QTEST_MAIN(TestEasing)
#include "test_easing.moc"
