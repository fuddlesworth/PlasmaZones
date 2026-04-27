// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorCurve.h>
#include <PhosphorAnimation/qml/PhosphorEasing.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>

#include <QJsonObject>
#include <QObject>
#include <QTest>
#include <QVariant>

using namespace PhosphorAnimation;

class TestPhosphorProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testMetaTypeRegistered()
    {
        PhosphorProfile p;
        QVariant v = QVariant::fromValue(p);
        QVERIFY(v.isValid());
        QVERIFY(v.canConvert<PhosphorProfile>());
    }

    /// On a default-constructed PhosphorProfile, all effective getters
    /// return Profile's library defaults — the wrapper collapses
    /// "unset" onto the effective-value projection per class doc.
    void testDefaultsReturnLibraryDefaults()
    {
        PhosphorProfile p;
        QCOMPARE(p.duration(), Profile::DefaultDuration);
        QCOMPARE(p.minDistance(), Profile::DefaultMinDistance);
        QCOMPARE(static_cast<int>(p.sequenceMode()), static_cast<int>(Profile::DefaultSequenceMode));
        QCOMPARE(p.staggerInterval(), Profile::DefaultStaggerInterval);
        QCOMPARE(p.presetName(), QString());
    }

    /// Writing a property engages the underlying optional. Reading
    /// back returns the engaged value, not the library default.
    void testPropertyWritesEngageOptional()
    {
        PhosphorProfile p;
        p.setDuration(300.0);
        p.setMinDistance(10);
        p.setStaggerInterval(50);
        p.setPresetName(QStringLiteral("snappy"));
        QCOMPARE(p.duration(), 300.0);
        QCOMPARE(p.minDistance(), 10);
        QCOMPARE(p.staggerInterval(), 50);
        QCOMPARE(p.presetName(), QStringLiteral("snappy"));

        // The underlying Profile's optionals are now engaged.
        QVERIFY(p.value().duration.has_value());
        QVERIFY(p.value().minDistance.has_value());
        QVERIFY(p.value().staggerInterval.has_value());
        QVERIFY(p.value().presetName.has_value());
    }

    void testSequenceModeEnumRoundTrip()
    {
        PhosphorProfile p;
        p.setSequenceMode(PhosphorProfile::SequenceMode::Cascade);
        QCOMPARE(p.sequenceMode(), PhosphorProfile::SequenceMode::Cascade);
        QCOMPARE(static_cast<int>(p.value().sequenceMode.value()),
                 static_cast<int>(PhosphorAnimation::SequenceMode::Cascade));
    }

    /// Curve property delegates to PhosphorCurve → the wrapped
    /// shared_ptr<const Curve> survives the round trip.
    void testCurveDelegate()
    {
        PhosphorProfile p;
        PhosphorCurve c = PhosphorCurve::fromEasing(PhosphorEasing());
        p.setCurve(c);
        QVERIFY(!p.curve().isNull());
        QCOMPARE(p.curve().typeId(), c.typeId());
    }

    /// toJson / fromJson roundtrip via the core Profile::toJson path.
    /// Verifies the QML wrapper preserves the engaged-only semantic.
    void testJsonRoundTrip()
    {
        PhosphorProfile p1;
        p1.setDuration(250.0);
        p1.setMinDistance(5);
        p1.setPresetName(QStringLiteral("custom"));

        QJsonObject json = p1.toJson();
        QVERIFY(json.contains(QStringLiteral("duration")));
        QVERIFY(json.contains(QStringLiteral("minDistance")));
        QVERIFY(json.contains(QStringLiteral("presetName")));
        // Unset field should be absent from the JSON.
        QVERIFY(!json.contains(QStringLiteral("staggerInterval")));

        PhosphorProfile p2 = PhosphorProfile::fromJson(json);
        QCOMPARE(p2.duration(), 250.0);
        QCOMPARE(p2.minDistance(), 5);
        QCOMPARE(p2.presetName(), QStringLiteral("custom"));
    }

    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorProfile::staticMetaObject;
        QVERIFY(mo->indexOfProperty("curve") >= 0);
        QVERIFY(mo->indexOfProperty("duration") >= 0);
        QVERIFY(mo->indexOfProperty("minDistance") >= 0);
        QVERIFY(mo->indexOfProperty("sequenceMode") >= 0);
        QVERIFY(mo->indexOfProperty("staggerInterval") >= 0);
        QVERIFY(mo->indexOfProperty("presetName") >= 0);
    }
};

QTEST_MAIN(TestPhosphorProfile)
#include "test_phosphorprofile.moc"
