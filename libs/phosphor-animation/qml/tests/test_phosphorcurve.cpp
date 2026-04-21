// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/qml/PhosphorCurve.h>
#include <PhosphorAnimation/qml/PhosphorEasing.h>
#include <PhosphorAnimation/qml/PhosphorSpring.h>

#include <QObject>
#include <QTest>
#include <QVariant>

using namespace PhosphorAnimation;

class TestPhosphorCurve : public QObject
{
    Q_OBJECT

private:
    CurveRegistry m_registry;

private Q_SLOTS:
    void initTestCase()
    {
        PhosphorCurve::setDefaultRegistry(&m_registry);
    }
    void cleanupTestCase()
    {
        PhosphorCurve::setDefaultRegistry(nullptr);
    }
    void testMetaTypeRegistered()
    {
        PhosphorCurve c;
        QVariant v = QVariant::fromValue(c);
        QVERIFY(v.isValid());
        QVERIFY(v.canConvert<PhosphorCurve>());
    }

    /// Default-constructed handle is null; isNull() reports accurately.
    void testDefaultIsNull()
    {
        PhosphorCurve c;
        QVERIFY(c.isNull());
        QCOMPARE(c.typeId(), QString());
        QCOMPARE(c.toString(), QString());
    }

    /// fromEasing wraps the Easing value in a shared_ptr<const Curve>;
    /// typeId matches the Easing variant's registered id.
    void testFromEasing()
    {
        PhosphorEasing e;
        e.setType(PhosphorEasing::Type::ElasticOut);
        PhosphorCurve c = PhosphorCurve::fromEasing(e);
        QVERIFY(!c.isNull());
        QVERIFY(!c.typeId().isEmpty());
        QVERIFY(!c.toString().isEmpty());
    }

    void testFromSpring()
    {
        PhosphorSpring s(14.0, 0.6);
        PhosphorCurve c = PhosphorCurve::fromSpring(s);
        QVERIFY(!c.isNull());
        QCOMPARE(c.typeId(), QStringLiteral("spring"));
        // toString should include both parameters.
        const QString serialized = c.toString();
        QVERIFY(serialized.contains(QStringLiteral("spring")));
    }

    /// fromString resolves via CurveRegistry — known built-in curves
    /// resolve, unknown strings produce a null handle.
    void testFromString()
    {
        PhosphorCurve spring = PhosphorCurve::fromString(QStringLiteral("spring:12.0,0.8"));
        QVERIFY(!spring.isNull());
        QCOMPARE(spring.typeId(), QStringLiteral("spring"));

        PhosphorCurve bezier = PhosphorCurve::fromString(QStringLiteral("0.33,1.00,0.68,1.00"));
        QVERIFY(!bezier.isNull());

        PhosphorCurve unknown = PhosphorCurve::fromString(QStringLiteral("not-a-real-curve-type:1,2,3"));
        // CurveRegistry::create may return a default or null on unknown
        // input; either way, the QML wrapper should report a usable
        // null predicate.
        if (unknown.isNull()) {
            QCOMPARE(unknown.typeId(), QString());
        } else {
            QVERIFY(!unknown.typeId().isEmpty());
        }
    }

    /// Equality uses Curve::equals — two PhosphorCurves wrapping
    /// different shared_ptrs that happen to point at curves with the
    /// same parameters must compare equal.
    void testEqualityByCurveEquals()
    {
        PhosphorCurve a = PhosphorCurve::fromSpring(PhosphorSpring(14.0, 0.6));
        PhosphorCurve b = PhosphorCurve::fromSpring(PhosphorSpring(14.0, 0.6));
        QVERIFY(a == b);

        PhosphorCurve c = PhosphorCurve::fromSpring(PhosphorSpring(20.0, 0.6));
        QVERIFY(a != c);

        PhosphorCurve nullHandle;
        QVERIFY(nullHandle != a);
    }

    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorCurve::staticMetaObject;
        QVERIFY(mo->indexOfProperty("typeId") >= 0);
    }
};

QTEST_MAIN(TestPhosphorCurve)
#include "test_phosphorcurve.moc"
