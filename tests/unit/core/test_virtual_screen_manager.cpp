// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen_manager.cpp
 * @brief Unit tests for ScreenManager virtual screen management methods
 *
 * Tests config storage (set/get), virtualScreenIdsFor(), hasVirtualScreens(),
 * effectiveScreenIds(), and the virtualScreensChanged signal.
 *
 * Note: Methods that depend on physical QScreen objects (screenGeometry,
 * virtualScreenAt, effectiveScreenAt) are not easily testable without a
 * display server, so we test the config-management layer that does not
 * require real QScreens.
 */

#include <QTest>
#include <QSignalSpy>

#include "core/screenmanager.h"
#include "core/virtualscreen.h"

using namespace PlasmaZones;

// Helper to build a simple two-way split config
static VirtualScreenConfig makeSplitConfig(const QString& physId)
{
    VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(
        VirtualScreenDef{VirtualScreenId::make(physId, 0), physId, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1.0), 0});
    config.screens.append(VirtualScreenDef{VirtualScreenId::make(physId, 1), physId, QStringLiteral("Right"),
                                           QRectF(0.5, 0, 0.5, 1.0), 1});
    return config;
}

// Helper to build a three-way split config
static VirtualScreenConfig makeThreeWayConfig(const QString& physId)
{
    VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(VirtualScreenDef{VirtualScreenId::make(physId, 0), physId, QStringLiteral("Left"),
                                           QRectF(0, 0, 0.333, 1.0), 0});
    config.screens.append(VirtualScreenDef{VirtualScreenId::make(physId, 1), physId, QStringLiteral("Center"),
                                           QRectF(0.333, 0, 0.334, 1.0), 1});
    config.screens.append(VirtualScreenDef{VirtualScreenId::make(physId, 2), physId, QStringLiteral("Right"),
                                           QRectF(0.667, 0, 0.333, 1.0), 2});
    return config;
}

class TestVirtualScreenManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // setVirtualScreenConfig / virtualScreenConfig round-trip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetAndGetConfig()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        auto config = makeSplitConfig(physId);

        mgr.setVirtualScreenConfig(physId, config);
        auto retrieved = mgr.virtualScreenConfig(physId);

        QCOMPARE(retrieved, config);
    }

    void testGetConfig_noConfig_returnsEmpty()
    {
        ScreenManager mgr;
        auto config = mgr.virtualScreenConfig(QStringLiteral("nonexistent"));

        QVERIFY(config.isEmpty());
        QVERIFY(!config.hasSubdivisions());
    }

    void testSetEmptyConfig_clearsSubdivisions()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Set a real config, then clear it
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        QVERIFY(mgr.hasVirtualScreens(physId));

        mgr.setVirtualScreenConfig(physId, VirtualScreenConfig{});
        QVERIFY(!mgr.hasVirtualScreens(physId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // hasVirtualScreens
    // ═══════════════════════════════════════════════════════════════════════════

    void testHasVirtualScreens_noConfig_returnsFalse()
    {
        ScreenManager mgr;
        QVERIFY(!mgr.hasVirtualScreens(QStringLiteral("Dell:U2722D:115107")));
    }

    void testHasVirtualScreens_withTwoScreens_returnsTrue()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QVERIFY(mgr.hasVirtualScreens(physId));
    }

    void testHasVirtualScreens_singleScreen_returnsFalse()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        VirtualScreenConfig config;
        config.physicalScreenId = physId;
        config.screens.append(
            VirtualScreenDef{VirtualScreenId::make(physId, 0), physId, QStringLiteral("Full"), QRectF(0, 0, 1, 1), 0});

        mgr.setVirtualScreenConfig(physId, config);
        // Single screen = no subdivision
        QVERIFY(!mgr.hasVirtualScreens(physId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // virtualScreenIdsFor
    // ═══════════════════════════════════════════════════════════════════════════

    void testVirtualScreenIdsFor_noConfig_returnsPhysicalId()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }

    void testVirtualScreenIdsFor_twoVirtualScreens()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0), VirtualScreenId::make(physId, 0));
        QCOMPARE(ids.at(1), VirtualScreenId::make(physId, 1));
    }

    void testVirtualScreenIdsFor_threeVirtualScreens()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("LG:27GP850:XYZ789");
        mgr.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 3);
        QCOMPARE(ids.at(0), VirtualScreenId::make(physId, 0));
        QCOMPARE(ids.at(1), VirtualScreenId::make(physId, 1));
        QCOMPARE(ids.at(2), VirtualScreenId::make(physId, 2));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveScreenIds (no QScreens tracked -> depends on virtual configs)
    // ═══════════════════════════════════════════════════════════════════════════

    void testEffectiveScreenIds_noScreensOrConfigs_returnsEmpty()
    {
        ScreenManager mgr;
        // No QScreens tracked and no virtual configs -> empty
        QStringList ids = mgr.effectiveScreenIds();
        QVERIFY(ids.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // virtualScreensChanged signal
    // ═══════════════════════════════════════════════════════════════════════════

    void testSignal_virtualScreensChanged_emittedOnConfigSet()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        QSignalSpy spy(&mgr, &ScreenManager::virtualScreensChanged);
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    void testSignal_virtualScreensChanged_emittedOnConfigClear()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QSignalSpy spy(&mgr, &ScreenManager::virtualScreensChanged);
        mgr.setVirtualScreenConfig(physId, VirtualScreenConfig{});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Config replacement
    // ═══════════════════════════════════════════════════════════════════════════

    void testConfigReplacement_overwritesPrevious()
    {
        ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 2);

        mgr.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Multiple physical screens with independent configs
    // ═══════════════════════════════════════════════════════════════════════════

    void testMultiplePhysicalScreens_independentConfigs()
    {
        ScreenManager mgr;
        const QString physA = QStringLiteral("Dell:U2722D:111");
        const QString physB = QStringLiteral("LG:27GP850:222");

        mgr.setVirtualScreenConfig(physA, makeSplitConfig(physA));
        mgr.setVirtualScreenConfig(physB, makeThreeWayConfig(physB));

        QCOMPARE(mgr.virtualScreenIdsFor(physA).size(), 2);
        QCOMPARE(mgr.virtualScreenIdsFor(physB).size(), 3);
        QVERIFY(mgr.hasVirtualScreens(physA));
        QVERIFY(mgr.hasVirtualScreens(physB));

        // Clear one, the other remains
        mgr.setVirtualScreenConfig(physA, VirtualScreenConfig{});
        QVERIFY(!mgr.hasVirtualScreens(physA));
        QVERIFY(mgr.hasVirtualScreens(physB));
    }
};

QTEST_GUILESS_MAIN(TestVirtualScreenManager)
#include "test_virtual_screen_manager.moc"
