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

    void testEngineConfiguratorCalledExactlyOnce()
    {
        int callCount = 0;
        PhosphorSurfaces::SurfaceManagerConfig config;
        config.engineConfigurator = [&callCount](QQmlEngine&) {
            ++callCount;
        };

        PhosphorSurfaces::SurfaceManager manager(std::move(config));
        QCOMPARE(callCount, 1);

        manager.createSurface({});
        QCOMPARE(callCount, 1);
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

    void testScopeGenerationStartsAtOne()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QCOMPARE(manager.nextScopeGeneration(), 1u);
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

    void testCreateSurfaceWithParent()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QObject customParent;
        PhosphorLayer::SurfaceConfig surfCfg;
        surfCfg.role = PhosphorLayer::Roles::FullscreenOverlay;
        surfCfg.contentUrl = QUrl(QStringLiteral("qrc:/nonexistent.qml"));

        auto* surface = manager.createSurface(std::move(surfCfg), &customParent);
        QVERIFY(surface == nullptr);
    }

    void testKeepAliveLostSignalExists()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QSignalSpy spy(&manager, &PhosphorSurfaces::SurfaceManager::keepAliveLost);
        QVERIFY(spy.isValid());
    }

    void testKeepAliveNotActiveWithoutFactory()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QCoreApplication::processEvents();

        QVERIFY(!manager.keepAliveActive());
    }

    void testPipelineCachePathStored()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        config.pipelineCachePath = QStringLiteral("/tmp/test-pipeline.cache");
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QVERIFY(manager.engine() != nullptr);
    }

    void testVulkanInstanceNullByDefault()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        QVERIFY(config.vulkanInstance == nullptr);
    }

    void testMultipleScopeGenerationsUnique()
    {
        PhosphorSurfaces::SurfaceManagerConfig config;
        PhosphorSurfaces::SurfaceManager manager(std::move(config));

        QSet<quint64> seen;
        for (int i = 0; i < 1000; ++i) {
            auto val = manager.nextScopeGeneration();
            QVERIFY(!seen.contains(val));
            seen.insert(val);
        }
        QCOMPARE(seen.size(), 1000);
    }

    void testDestructionWithoutCrash()
    {
        auto manager = std::make_unique<PhosphorSurfaces::SurfaceManager>(PhosphorSurfaces::SurfaceManagerConfig{});

        QVERIFY(manager->engine() != nullptr);

        auto gen1 = manager->nextScopeGeneration();
        QVERIFY(gen1 > 0);

        manager.reset();
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
