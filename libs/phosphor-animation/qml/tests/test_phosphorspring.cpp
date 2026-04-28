// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorSpring.h>

#include <QMetaType>
#include <QObject>
#include <QTest>
#include <QVariant>

using namespace PhosphorAnimation;

class TestPhosphorSpring : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testMetaTypeRegistered()
    {
        PhosphorSpring s;
        QVariant v = QVariant::fromValue(s);
        QVERIFY(v.isValid());
        QVERIFY(v.canConvert<PhosphorSpring>());
    }

    void testDefaults()
    {
        // Default Spring parameters from the core library.
        PhosphorSpring s;
        QCOMPARE(s.omega(), Spring().omega);
        QCOMPARE(s.zeta(), Spring().zeta);
    }

    void testParameterSetters()
    {
        PhosphorSpring s;
        s.setOmega(20.0);
        s.setZeta(0.5);
        QCOMPARE(s.omega(), 20.0);
        QCOMPARE(s.zeta(), 0.5);
    }

    /// Writes through the setters must go through the Spring ctor
    /// (which applies parameter-clamp). Direct field writes would
    /// bypass the clamp — this test asserts the setter path is the
    /// clamped one.
    void testSetterClampsToValidRange()
    {
        PhosphorSpring s;
        // Out-of-range omega — Spring ctor clamps to [0.1, 200].
        s.setOmega(500.0);
        QVERIFY(s.omega() <= 200.0);
        QVERIFY(s.omega() >= 0.1);

        s.setZeta(-5.0);
        QVERIFY(s.zeta() >= 0.0);
        QVERIFY(s.zeta() <= 10.0);
    }

    void testStringRoundTrip()
    {
        const PhosphorSpring s1 = PhosphorSpring::fromString(QStringLiteral("spring:14.0,0.6"));
        QCOMPARE(s1.omega(), 14.0);
        QCOMPARE(s1.zeta(), 0.6);

        const QString serialized = s1.toString();
        const PhosphorSpring s2 = PhosphorSpring::fromString(serialized);
        QVERIFY(s1 == s2);
    }

    /// Preset factories are the QML-reachable version of the C++
    /// Spring::snappy() / smooth() / bouncy() named tunings. Verify
    /// they produce equal-parameter values.
    void testPresetFactories()
    {
        PhosphorSpring fromFactory = PhosphorSpring::snappy();
        Spring fromCore = Spring::snappy();
        QCOMPARE(fromFactory.omega(), fromCore.omega);
        QCOMPARE(fromFactory.zeta(), fromCore.zeta);

        QVERIFY(PhosphorSpring::smooth() != PhosphorSpring::snappy());
        QVERIFY(PhosphorSpring::bouncy() != PhosphorSpring::smooth());
    }

    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorSpring::staticMetaObject;
        QVERIFY(mo->indexOfProperty("omega") >= 0);
        QVERIFY(mo->indexOfProperty("zeta") >= 0);
    }
};

QTEST_MAIN(TestPhosphorSpring)
#include "test_phosphorspring.moc"
