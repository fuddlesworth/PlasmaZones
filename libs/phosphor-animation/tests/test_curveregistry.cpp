// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

using PhosphorAnimation::Curve;
using PhosphorAnimation::CurveRegistry;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;

class TestCurveRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Built-ins ───

    void testBuiltInEasingTypes()
    {
        auto& reg = CurveRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("bezier")));
        QVERIFY(reg.has(QStringLiteral("elastic-in")));
        QVERIFY(reg.has(QStringLiteral("elastic-out")));
        QVERIFY(reg.has(QStringLiteral("elastic-in-out")));
        QVERIFY(reg.has(QStringLiteral("bounce-in")));
        QVERIFY(reg.has(QStringLiteral("bounce-out")));
        QVERIFY(reg.has(QStringLiteral("bounce-in-out")));
    }

    void testBuiltInSpring()
    {
        auto& reg = CurveRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("spring")));
    }

    // ─── create() parsing ───

    void testCreateLegacyBezier()
    {
        // Bare "x1,y1,x2,y2" form routes to bezier factory.
        auto curve = CurveRegistry::instance().create(QStringLiteral("0.33,1.00,0.68,1.00"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    void testCreatePrefixedBezier()
    {
        auto curve = CurveRegistry::instance().create(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    void testCreateElastic()
    {
        auto curve = CurveRegistry::instance().create(QStringLiteral("elastic-out:1.2,0.4"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("elastic-out"));
    }

    void testCreateSpring()
    {
        auto curve = CurveRegistry::instance().create(QStringLiteral("spring:12.0,0.8"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("spring"));
        QVERIFY(curve->isStateful());
    }

    void testCreateEmptyReturnsNull()
    {
        auto curve = CurveRegistry::instance().create(QString());
        QVERIFY(curve == nullptr);
    }

    void testCreateUnknownFallsBack()
    {
        // Unknown typeId falls through to a default bezier instead of
        // returning null — callers get a valid curve to work with.
        auto curve = CurveRegistry::instance().create(QStringLiteral("not-a-real-curve:1,2,3"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    // ─── Extension ───

    void testRegisterAndUnregisterCustom()
    {
        auto& reg = CurveRegistry::instance();
        const QString id = QStringLiteral("test-custom-linear");

        QVERIFY(!reg.has(id));

        CurveRegistry::Factory factory = [](const QString&, const QString&) -> std::shared_ptr<const Curve> {
            // Linear curve — reuse Easing with bezier (0,0,1,1) shape.
            Easing e;
            e.x1 = 0.0;
            e.y1 = 0.0;
            e.x2 = 1.0;
            e.y2 = 1.0;
            return std::make_shared<Easing>(e);
        };

        const bool replaced = reg.registerFactory(id, factory);
        QVERIFY(!replaced);
        QVERIFY(reg.has(id));

        auto curve = reg.create(id);
        QVERIFY(curve != nullptr);
        // At t=0.5, linear → 0.5 within tolerance.
        QVERIFY(qAbs(curve->evaluate(0.5) - 0.5) < 0.05);

        QVERIFY(reg.unregisterFactory(id));
        QVERIFY(!reg.has(id));
    }

    void testRegisterEmptyRejected()
    {
        auto& reg = CurveRegistry::instance();
        const bool result = reg.registerFactory(QString(), [](const QString&, const QString&) {
            return std::make_shared<Easing>();
        });
        QVERIFY(!result);
    }

    void testKnownTypesContainsBuiltIns()
    {
        const QStringList types = CurveRegistry::instance().knownTypes();
        QVERIFY(types.contains(QStringLiteral("bezier")));
        QVERIFY(types.contains(QStringLiteral("spring")));
        QVERIFY(types.contains(QStringLiteral("elastic-out")));
    }
};

QTEST_MAIN(TestCurveRegistry)
#include "test_curveregistry.moc"
