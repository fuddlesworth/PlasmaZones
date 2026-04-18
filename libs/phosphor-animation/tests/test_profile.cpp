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
using PhosphorAnimation::Spring;

class TestProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaults()
    {
        Profile p;
        QVERIFY(p.curve == nullptr); // null = "inherit"
        QCOMPARE(p.duration, 150.0);
        QCOMPARE(p.minDistance, 0);
        QCOMPARE(p.sequenceMode, 0);
        QCOMPARE(p.staggerInterval, 30);
        QVERIFY(p.presetName.isEmpty());
    }

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

    void testEqualityFieldsOnly()
    {
        Profile a;
        Profile b;
        a.duration = 300.0;
        QVERIFY(a != b);
        b.duration = 300.0;
        QCOMPARE(a, b);
    }

    // ─── Serialization ───

    void testToJsonOmitsNullCurveAndEmptyPreset()
    {
        Profile p;
        const QJsonObject obj = p.toJson();
        QVERIFY(!obj.contains(QLatin1String("curve"))); // null curve omitted
        QVERIFY(!obj.contains(QLatin1String("presetName"))); // empty omitted
        QVERIFY(obj.contains(QLatin1String("duration")));
        QVERIFY(obj.contains(QLatin1String("staggerInterval")));
    }

    void testRoundTripEasing()
    {
        Profile original;
        original.curve = std::make_shared<Easing>();
        original.duration = 250.0;
        original.minDistance = 15;
        original.sequenceMode = 1;
        original.staggerInterval = 45;
        original.presetName = QStringLiteral("My Fast");

        const Profile restored = Profile::fromJson(original.toJson());
        QCOMPARE(restored, original);
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

    void testFromJsonMissingFieldsUsesDefaults()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 999.0);
        const Profile p = Profile::fromJson(obj);
        QCOMPARE(p.duration, 999.0);
        QCOMPARE(p.minDistance, 0);
        QCOMPARE(p.staggerInterval, 30);
        QVERIFY(p.curve == nullptr);
    }
};

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
