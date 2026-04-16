// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQuickItem>
#include <QTest>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

class TestFactory : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void missingTransportYieldsNullptr()
    {
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(nullptr, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        QCOMPARE(f.create(std::move(cfg)), nullptr);
    }

    void missingScreenProviderYieldsNullptr()
    {
        MockTransport t;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, nullptr));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        QCOMPARE(f.create(std::move(cfg)), nullptr);
    }

    void factoryIsParentByDefault()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        QCOMPARE(surface->parent(), &f);
    }

    void explicitParentOverridesFactory()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        QObject owner;
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        auto* surface = f.create(std::move(cfg), &owner);
        QVERIFY(surface);
        QCOMPARE(surface->parent(), &owner);
    }

    void depsAccessorReflectsInjection()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        QCOMPARE(f.deps().transport, &t);
        QCOMPARE(f.deps().screens, &s);
        QCOMPARE(f.deps().engineProvider, nullptr);
    }
};

QTEST_MAIN(TestFactory)
#include "test_factory.moc"
