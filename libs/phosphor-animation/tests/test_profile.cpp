// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

#include <limits>

using PhosphorAnimation::Curve;
using PhosphorAnimation::CurveRegistry;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Profile;
using PhosphorAnimation::SequenceMode;
using PhosphorAnimation::Spring;

class TestProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultAllOptionalsUnset()
    {
        Profile p;
        QVERIFY(p.curve == nullptr); // null = "inherit"
        QVERIFY(!p.duration.has_value());
        QVERIFY(!p.minDistance.has_value());
        QVERIFY(!p.sequenceMode.has_value());
        QVERIFY(!p.staggerInterval.has_value());
        QVERIFY(!p.presetName.has_value());
    }

    void testEffectiveGettersUseLibraryDefaults()
    {
        // An unset Profile reads the library defaults through the
        // effective* accessors — so consumers never see a raw nullopt.
        Profile p;
        QCOMPARE(p.effectiveDuration(), Profile::DefaultDuration);
        QCOMPARE(p.effectiveMinDistance(), Profile::DefaultMinDistance);
        QCOMPARE(p.effectiveSequenceMode(), Profile::DefaultSequenceMode);
        QCOMPARE(p.effectiveStaggerInterval(), Profile::DefaultStaggerInterval);
    }

    void testWithDefaultsFillsAllOptionals()
    {
        Profile empty;
        const Profile filled = empty.withDefaults();
        QVERIFY(filled.duration.has_value());
        QVERIFY(filled.minDistance.has_value());
        QVERIFY(filled.sequenceMode.has_value());
        QVERIFY(filled.staggerInterval.has_value());
        // curve is filled with a library-default OutCubic Easing — callers
        // that pass through withDefaults() get a fully-populated Profile.
        // The "curve left null" shape from before was a trap where
        // downstream code still had to null-check.
        QVERIFY(filled.curve != nullptr);
        QCOMPARE(filled.curve->typeId(), QStringLiteral("bezier"));
    }

    void testWithDefaultsPreservesExistingCurve()
    {
        Profile p;
        auto spring = std::make_shared<Spring>(Spring::bouncy());
        p.curve = spring;
        const Profile filled = p.withDefaults();
        // An existing curve must not be replaced by the library default.
        QCOMPARE(filled.curve.get(), spring.get());
    }

    // ─── Equality ───

    void testEqualityWithSameValues()
    {
        Profile a;
        Profile b;
        QCOMPARE(a, b);

        a.curve = std::make_shared<Spring>(Spring::snappy());
        b.curve = std::make_shared<Spring>(Spring::snappy());
        QCOMPARE(a, b); // equal curves are value-equal even if different shared_ptrs
    }

    void testEqualityDifferentCurveSubclass()
    {
        Profile a;
        a.curve = std::make_shared<Easing>();
        Profile b;
        b.curve = std::make_shared<Spring>();
        QVERIFY(a != b);
    }

    void testEqualityNullVsConcrete()
    {
        Profile a;
        Profile b;
        b.curve = std::make_shared<Easing>();
        QVERIFY(a != b);
    }

    void testEqualityUnsetVsExplicitDefault()
    {
        // A Profile with duration unset is NOT equal to one with
        // duration explicitly set to the default. This is the whole
        // point of optional-based fields — "I didn't say" and "I said
        // the default" are different.
        Profile unset;
        Profile explicitDefault;
        explicitDefault.duration = Profile::DefaultDuration;
        QVERIFY(unset != explicitDefault);
    }

    void testEqualityFieldsOnly()
    {
        Profile a;
        Profile b;
        a.duration = 300.0;
        QVERIFY(a != b);
        b.duration = 300.0;
        QCOMPARE(a, b);
    }

    void testEqualityPolymorphicCurveUsesTightComparison()
    {
        // Two Easings differing below toString() precision (0.005) must
        // compare unequal through the polymorphic curve->equals() path,
        // not equal via a lossy string round-trip.
        auto a = std::make_shared<Easing>();
        a->type = Easing::Type::ElasticOut;
        a->amplitude = 1.003;
        auto b = std::make_shared<Easing>();
        b->type = Easing::Type::ElasticOut;
        b->amplitude = 1.006;

        Profile pa;
        pa.curve = a;
        Profile pb;
        pb.curve = b;
        QVERIFY(pa != pb);
    }

    // ─── Serialization ───

    void testToJsonOmitsUnsetFields()
    {
        Profile p;
        const QJsonObject obj = p.toJson();
        // Every optional field is unset → omitted. Only an empty object.
        QVERIFY(!obj.contains(QLatin1String("curve")));
        QVERIFY(!obj.contains(QLatin1String("duration")));
        QVERIFY(!obj.contains(QLatin1String("minDistance")));
        QVERIFY(!obj.contains(QLatin1String("sequenceMode")));
        QVERIFY(!obj.contains(QLatin1String("staggerInterval")));
        QVERIFY(!obj.contains(QLatin1String("presetName")));
    }

    void testToJsonIncludesExplicitlySetDefault()
    {
        // "duration = 150" (explicitly set, even if equal to library
        // default) MUST appear in JSON — otherwise the override is
        // silently dropped and the parent wins when reloaded.
        Profile p;
        p.duration = Profile::DefaultDuration;
        const QJsonObject obj = p.toJson();
        QVERIFY(obj.contains(QLatin1String("duration")));
    }

    void testRoundTripEasing()
    {
        Profile original;
        original.curve = std::make_shared<Easing>();
        original.duration = 250.0;
        original.minDistance = 15;
        original.sequenceMode = SequenceMode::Cascade;
        original.staggerInterval = 45;
        original.presetName = QStringLiteral("My Fast");

        const Profile restored = Profile::fromJson(original.toJson(), CurveRegistry{});
        QCOMPARE(restored, original);
    }

    void testPresetNameEngagedEmptyOverridesParent()
    {
        // Regression: presetName is std::optional<QString>, so an engaged
        // empty-string override is distinct from a nullopt "inherit".
        // `toJson` must emit an engaged-but-empty name; `fromJson` must
        // round-trip it back to engaged-empty, not drop it as "unset".
        Profile p;
        p.presetName = QString();
        const QJsonObject obj = p.toJson();
        QVERIFY(obj.contains(QLatin1String("presetName")));

        const Profile restored = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(restored.presetName.has_value());
        QVERIFY(restored.presetName->isEmpty());
    }

    void testPresetNameAbsentVsEngagedEmptyDistinguishedInJson()
    {
        // Belt-and-braces around the QJsonObject "" vs missing-key
        // contract: a Profile with no presetName must round-trip with
        // the field absent; a Profile with engaged-empty presetName
        // must round-trip with the field present-and-empty. Equality
        // distinguishes the two cases, which is the whole point of
        // optional-bearing fields.
        Profile absent;
        Profile engagedEmpty;
        engagedEmpty.presetName = QString();

        QVERIFY(absent != engagedEmpty);

        const Profile absentRestored = Profile::fromJson(absent.toJson(), CurveRegistry{});
        const Profile engagedEmptyRestored = Profile::fromJson(engagedEmpty.toJson(), CurveRegistry{});

        QVERIFY(!absentRestored.presetName.has_value());
        QVERIFY(engagedEmptyRestored.presetName.has_value());
        QVERIFY(absentRestored != engagedEmptyRestored);
    }

    void testRoundTripSpring()
    {
        Profile original;
        original.curve = std::make_shared<Spring>(Spring::bouncy());
        original.duration = 400.0;

        const Profile restored = Profile::fromJson(original.toJson(), CurveRegistry{});
        QCOMPARE(restored.duration, original.duration);
        QVERIFY(restored.curve != nullptr);
        QCOMPARE(restored.curve->typeId(), QStringLiteral("spring"));
        QVERIFY(restored.curve->equals(*original.curve));
    }

    void testFromJsonMissingFieldsLeaveUnset()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 999.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QCOMPARE(*p.duration, 999.0);
        QVERIFY(!p.minDistance.has_value());
        QVERIFY(!p.staggerInterval.has_value());
        QVERIFY(p.curve == nullptr);
    }

    void testFromJsonSequenceModeClamps()
    {
        // Unknown integer values fall back to the library default enum.
        QJsonObject obj;
        obj.insert(QLatin1String("sequenceMode"), 99);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QCOMPARE(*p.sequenceMode, Profile::DefaultSequenceMode);
    }

    // ─── Input validation (H1) ───

    void testFromJsonRejectsNegativeDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), -150.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        // Negative duration rejected → duration stays unset → effective
        // reads library default.
        QVERIFY(!p.duration.has_value());
        QCOMPARE(p.effectiveDuration(), Profile::DefaultDuration);
    }

    void testFromJsonRejectsZeroDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 0.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    void testFromJsonRejectsNonFiniteDuration()
    {
        // JSON parsers typically reject NaN/Infinity as JSON numbers,
        // but QJsonValue::toDouble accepts a default that a caller
        // could in theory coerce. Guard via the explicit isfinite()
        // check — this covers any downstream where a malformed value
        // sneaks in.
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), QJsonValue(std::numeric_limits<double>::infinity()));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    void testFromJsonRejectsAbsurdlyLargeDuration()
    {
        QJsonObject obj;
        // Two hours — past the 1-hour sanity bound. Accepting would
        // risk int-overflow via qRound() downstream.
        obj.insert(QLatin1String("duration"), 2.0 * 60.0 * 60.0 * 1000.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    void testFromJsonAcceptsLargeButReasonableDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 5000.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.duration.has_value());
        QCOMPARE(*p.duration, 5000.0);
    }

    void testFromJsonRejectsNegativeMinDistance()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), -5);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.minDistance.has_value());
        QCOMPARE(p.effectiveMinDistance(), Profile::DefaultMinDistance);
    }

    void testFromJsonAcceptsZeroMinDistance()
    {
        // Zero = "no skip threshold" — the documented default. Must
        // be accepted as a valid explicit setting distinct from
        // "unset".
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), 0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.minDistance.has_value());
        QCOMPARE(*p.minDistance, 0);
    }
};

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
