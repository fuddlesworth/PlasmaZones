// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

using PhosphorAnimation::Curve;
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
        // curve stays null — runtime can substitute default Easing if needed.
        QVERIFY(filled.curve == nullptr);
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

        const Profile restored = Profile::fromJson(original.toJson());
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

        const Profile restored = Profile::fromJson(obj);
        QVERIFY(restored.presetName.has_value());
        QVERIFY(restored.presetName->isEmpty());
    }

    void testRoundTripSpring()
    {
        Profile original;
        original.curve = std::make_shared<Spring>(Spring::bouncy());
        original.duration = 400.0;

        const Profile restored = Profile::fromJson(original.toJson());
        QCOMPARE(restored.duration, original.duration);
        QVERIFY(restored.curve != nullptr);
        QCOMPARE(restored.curve->typeId(), QStringLiteral("spring"));
        QVERIFY(restored.curve->equals(*original.curve));
    }

    void testFromJsonMissingFieldsLeaveUnset()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 999.0);
        const Profile p = Profile::fromJson(obj);
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
        const Profile p = Profile::fromJson(obj);
        QCOMPARE(*p.sequenceMode, Profile::DefaultSequenceMode);
    }
};

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
