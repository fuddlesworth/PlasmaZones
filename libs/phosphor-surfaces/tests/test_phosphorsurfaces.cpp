// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorSurfaces/SurfaceManagerConfig.h>

#include <QSignalSpy>
#include <QTest>

class TestPhosphorSurfaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testConstructionWithoutFactory()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QVERIFY(manager.engine() != nullptr);
        QVERIFY(!manager.keepAliveActive());
    }

    void testEngineConfiguratorCalled()
    {
        bool called = false;
        PhosphorSurfaces::SurfaceManagerConfig config;
        config.engineConfigurator = [&called](QQmlEngine&) {
            called = true;
        };

        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QVERIFY(called);
        QVERIFY(manager.engine() != nullptr);
    }

    void testWindowConfiguratorStored()
    {
        bool called = false;
        PhosphorSurfaces::SurfaceManagerConfig config;
        config.windowConfigurator = [&called](QQuickWindow&) {
            called = true;
        };

        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        // windowConfigurator is not called during construction (no windows yet),
        // only when createSurface / createKeepAlive produce a window.
        QVERIFY(!called);
    }

    void testScopeGenerationMonotonic()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        const quint64 first = manager.nextScopeGeneration();
        const quint64 second = manager.nextScopeGeneration();
        const quint64 third = manager.nextScopeGeneration();

        QCOMPARE(first, 1u);
        QCOMPARE(second, 2u);
        QCOMPARE(third, 3u);
        QVERIFY(first < second);
        QVERIFY(second < third);
    }

    void testCreateSurfaceWithoutFactory()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        PhosphorLayer::SurfaceConfig surfCfg;
        surfCfg.role = PhosphorLayer::Roles::FullscreenOverlay;
        surfCfg.contentUrl = QUrl(QStringLiteral("qrc:/nonexistent.qml"));

        auto* surface = manager.createSurface(std::move(surfCfg));
        QVERIFY(surface == nullptr);
    }

    void testKeepAliveLostSignalExists()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QSignalSpy spy(&manager, &PhosphorSurfaces::SurfaceManager::keepAliveLost);
        QVERIFY(spy.isValid());
    }
};

#include <QGuiApplication>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestPhosphorSurfaces tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_phosphorsurfaces.moc"
