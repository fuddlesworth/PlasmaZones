// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationQml/PhosphorEasing.h>

#include <QMetaType>
#include <QObject>
#include <QTest>
#include <QVariant>

using namespace PhosphorAnimation;

class TestPhosphorEasing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Q_GADGET must be registered and round-trippable through QVariant —
    /// the QML property-binding path relies on this.
    void testMetaTypeRegistered()
    {
        PhosphorEasing e;
        QVariant v = QVariant::fromValue(e);
        QVERIFY(v.isValid());
        QVERIFY(v.canConvert<PhosphorEasing>());
        const PhosphorEasing back = v.value<PhosphorEasing>();
        QVERIFY(back == e);
    }

    /// Property setter/getter delegates to the wrapped Easing. Tests
    /// each parameter independently so a missed setter shows up as a
    /// specific failure rather than a blanket "nothing works."
    void testPropertyDelegates()
    {
        PhosphorEasing e;
        // Default is CubicBezier
        QCOMPARE(e.type(), PhosphorEasing::Type::CubicBezier);

        e.setX1(0.1);
        e.setY1(0.2);
        e.setX2(0.3);
        e.setY2(0.4);
        QCOMPARE(e.x1(), 0.1);
        QCOMPARE(e.y1(), 0.2);
        QCOMPARE(e.x2(), 0.3);
        QCOMPARE(e.y2(), 0.4);

        e.setAmplitude(1.5);
        e.setPeriod(0.5);
        e.setBounces(4);
        QCOMPARE(e.amplitude(), 1.5);
        QCOMPARE(e.period(), 0.5);
        QCOMPARE(e.bounces(), 4);
    }

    /// Enum round-trip through int values — matches decision O's "do
    /// not rename enum values" convention so static_cast between the
    /// QML wrapper's Type and PhosphorAnimation::Easing::Type is safe.
    void testEnumRoundTrip()
    {
        PhosphorEasing e;
        e.setType(PhosphorEasing::Type::ElasticOut);
        QCOMPARE(e.type(), PhosphorEasing::Type::ElasticOut);
        QCOMPARE(static_cast<int>(e.value().type), static_cast<int>(Easing::Type::ElasticOut));

        e.setType(PhosphorEasing::Type::BounceInOut);
        QCOMPARE(e.type(), PhosphorEasing::Type::BounceInOut);
        QCOMPARE(static_cast<int>(e.value().type), static_cast<int>(Easing::Type::BounceInOut));
    }

    /// fromString/toString go through the core Easing wire format.
    /// Verify the same string survives the round trip.
    void testStringRoundTrip()
    {
        const PhosphorEasing e1 = PhosphorEasing::fromString(QStringLiteral("elastic-out:1.5,0.3"));
        QCOMPARE(e1.type(), PhosphorEasing::Type::ElasticOut);
        QCOMPARE(e1.amplitude(), 1.5);
        QCOMPARE(e1.period(), 0.3);

        const QString serialized = e1.toString();
        const PhosphorEasing e2 = PhosphorEasing::fromString(serialized);
        QVERIFY(e1 == e2);
    }

    /// Properties must be reachable via QMetaObject — the QML binding
    /// engine uses meta-reflection to evaluate bindings. A missing
    /// Q_PROPERTY declaration would fail this check without breaking
    /// direct C++ access.
    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorEasing::staticMetaObject;
        QVERIFY(mo->indexOfProperty("type") >= 0);
        QVERIFY(mo->indexOfProperty("x1") >= 0);
        QVERIFY(mo->indexOfProperty("y1") >= 0);
        QVERIFY(mo->indexOfProperty("x2") >= 0);
        QVERIFY(mo->indexOfProperty("y2") >= 0);
        QVERIFY(mo->indexOfProperty("amplitude") >= 0);
        QVERIFY(mo->indexOfProperty("period") >= 0);
        QVERIFY(mo->indexOfProperty("bounces") >= 0);
    }

    /// Implicit conversion ctor from a core-library Easing value lets
    /// C++ adapter code hand an Easing across the QML boundary without
    /// writing an explicit wrap call.
    void testImplicitConversionFromCoreValue()
    {
        Easing core = Easing::fromString(QStringLiteral("elastic-in:2.0,0.4"));
        PhosphorEasing wrapped = core;
        QCOMPARE(wrapped.type(), PhosphorEasing::Type::ElasticIn);
        QCOMPARE(wrapped.amplitude(), core.amplitude);
    }
};

QTEST_MAIN(TestPhosphorEasing)
#include "test_phosphoreasing.moc"
